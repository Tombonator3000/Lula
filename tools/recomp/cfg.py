"""Code discovery for the static recompiler.

A "function" here is the set of instructions reachable from an entry point
without following calls. Code shared between functions (for example a common
epilogue reached by jmp) is simply lifted into every function that reaches it.
A direct jmp to another known entry point is a tail call.

Entry points come from the PE entry, the Ghidra function list, direct call
targets and every relocated pointer into the code section that is not part of
a jump table (vtables, callbacks, function pointers in data).
"""
import capstone
from capstone import x86 as X

from . import pe as pe_mod

COND_JUMPS = {'jo', 'jno', 'jb', 'jae', 'je', 'jne', 'jbe', 'ja', 'js', 'jns',
              'jp', 'jnp', 'jl', 'jge', 'jle', 'jg'}
LOOPS = {'loop', 'loope', 'loopne', 'jecxz'}
# Instructions that end a path and are lifted as runtime traps.
TRAPS = {'int3', 'hlt', 'int', 'into', 'in', 'out', 'insb', 'insd', 'outsb', 'outsd',
         'lcall', 'ljmp', 'iretd', 'retf', 'sldt', 'arpl', 'lgdt', 'lidt', 'cli', 'sti',
         'ud2', 'invalid'}


class Insn:
    __slots__ = ('addr', 'size', 'cs', 'mnem', 'prefix', 'ops', 'flow', 'targets',
                 'table', 'iat', 'next')

    def __init__(self, cs_insn):
        self.addr = cs_insn.address
        self.size = cs_insn.size
        self.cs = cs_insn
        parts = cs_insn.mnemonic.split()
        self.prefix = parts[:-1]          # e.g. ['rep'] or ['repne']
        self.mnem = parts[-1]
        # Capstone leaves F2/F3 out of the mnemonic for some string
        # instructions (Watcom's memcpy uses 'repnz movsd', F2 A5), so read
        # the prefix bytes themselves.
        if not self.prefix and self.mnem[:4] in ('movs', 'stos', 'lods', 'scas', 'cmps'):
            for byte in cs_insn.bytes:
                if byte == 0xf3:
                    self.prefix = ['rep']
                elif byte == 0xf2:
                    self.prefix = ['repne']
                elif byte not in (0xf0, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0x66, 0x67):
                    break
        self.ops = list(cs_insn.operands)
        self.flow = 'seq'                 # seq, jcc, jmp, jmpi, call, calli, ret, trap
        self.targets = []                 # static successor/target addresses
        self.table = None                 # jump table: (table_va, [targets])
        self.iat = None                   # IAT slot for call/jmp [slot]
        self.next = self.addr + self.size

    def __repr__(self):
        return f'{self.addr:#x}: {self.cs.mnemonic} {self.cs.op_str}'


class Function:
    def __init__(self, entry):
        self.entry = entry
        self.insns = {}            # addr -> Insn
        self.leaders = set()       # addresses that need a C label
        self.tailcalls = set()     # entry addresses jumped to
        self.calls = set()         # direct call targets
        self.bad = []              # decode problems


class Program:
    def __init__(self, image):
        self.img = image
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self._cache = {}
        self.entries = set()
        self.functions = {}
        self.table_slots = set()   # VAs of dwords that belong to jump tables
        self.table_vas = set()
        self.notes = []

    # -- decoding -------------------------------------------------------
    def decode(self, addr):
        ins = self._cache.get(addr)
        if ins is not None:
            return ins
        if not self.img.in_text(addr):
            return None
        raw = self.img.read(addr, 16)
        cs = next(self.md.disasm(raw, addr, 1), None)
        if cs is None:
            return None
        ins = Insn(cs)
        self._classify(ins)
        self._cache[addr] = ins
        return ins

    def _classify(self, ins):
        m = ins.mnem
        groups = ins.cs.groups
        op0 = ins.ops[0] if ins.ops else None
        if m in TRAPS:
            ins.flow = 'trap'
        elif m in COND_JUMPS or m in LOOPS:
            ins.flow = 'jcc'
            ins.targets = [op0.imm, ins.next]
        elif m == 'jmp':
            if op0.type == X.X86_OP_IMM:
                ins.flow = 'jmp'
                ins.targets = [op0.imm]
            else:
                ins.flow = 'jmpi'
                self._indirect_target(ins, op0)
        elif m == 'call':
            if op0.type == X.X86_OP_IMM:
                ins.flow = 'call'
                ins.targets = [op0.imm]
            else:
                ins.flow = 'calli'
                self._indirect_target(ins, op0)
        elif capstone.CS_GRP_RET in groups:
            ins.flow = 'ret'

    def _indirect_target(self, ins, op):
        if op.type == X.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            slot = op.mem.disp & 0xffffffff
            if slot in self.img.imports:
                ins.iat = slot

    # -- jump tables ----------------------------------------------------
    def _jump_table(self, func, ins):
        op = ins.ops[0]
        if op.type != X.X86_OP_MEM or op.mem.base != 0 and op.mem.index != 0:
            return None
        if op.mem.index == 0 and op.mem.base == 0:
            return None
        table = op.mem.disp & 0xffffffff
        if not self.img.in_text(table) and table not in self.img.relocs:
            pass
        # Contiguous relocated dwords pointing into the code section.
        count = 0
        while (table + 4 * count) in self.img.relocs and \
                self.img.in_text(self.img.u32(table + 4 * count)) and \
                (count == 0 or (table + 4 * count) not in self.table_vas):
            count += 1
        if count == 0:
            return None
        bound = self._switch_bound(func, ins, op)
        if bound is not None and bound < count:
            count = bound
        targets = [self.img.u32(table + 4 * i) for i in range(count)]
        self.table_vas.add(table)
        for i in range(count):
            self.table_slots.add(table + 4 * i)
        return table, targets

    def _switch_bound(self, func, ins, op):
        """Find 'cmp idx, imm; ja/jbe' guarding a scaled jump table."""
        idx = op.mem.index if op.mem.index else op.mem.base
        scale = op.mem.scale if op.mem.index else 1
        if scale != 4:
            return None
        # Walk backwards over straight-line predecessors in this function.
        addr_list = sorted(a for a in func.insns if a < ins.addr)[-12:]
        for a in reversed(addr_list):
            p = func.insns[a]
            if p.mnem == 'cmp' and len(p.ops) == 2 and p.ops[0].type == X.X86_OP_REG \
                    and p.ops[0].reg == idx and p.ops[1].type == X.X86_OP_IMM:
                after = func.insns.get(p.next)
                if after and after.mnem in ('ja', 'jbe'):
                    return (p.ops[1].imm & 0xffffffff) + 1
                if after and after.mnem in ('jae', 'jb'):
                    return p.ops[1].imm & 0xffffffff
                return None
            if p.flow != 'seq' and p.flow != 'jcc':
                return None
            # The index register must not be redefined between cmp and jmp.
            if p.ops and p.ops[0].type == X.X86_OP_REG and p.ops[0].reg == idx \
                    and p.mnem not in ('cmp', 'test'):
                return None
        return None

    # -- functions ------------------------------------------------------
    def build_function(self, entry):
        func = Function(entry)
        work = [entry]
        func.leaders.add(entry)
        pending_tables = []
        while work:
            a = work.pop()
            while a not in func.insns:
                ins = self.decode(a)
                if ins is None:
                    func.bad.append(a)
                    break
                # Overlap check against a previously decoded neighbour.
                func.insns[a] = ins
                if ins.flow == 'seq' or ins.flow in ('call', 'calli'):
                    if ins.flow == 'call':
                        func.calls.add(ins.targets[0])
                    a = ins.next
                    continue
                if ins.flow == 'jcc':
                    t = ins.targets[0]
                    func.leaders.add(t)
                    work.append(t)
                    func.leaders.add(ins.next)
                    a = ins.next
                    continue
                if ins.flow == 'jmp':
                    t = ins.targets[0]
                    if t != entry and t in self.entries:
                        func.tailcalls.add(t)
                    else:
                        func.leaders.add(t)
                        work.append(t)
                    break
                if ins.flow == 'jmpi':
                    if ins.iat is None:
                        pending_tables.append(ins)
                    break
                break  # ret, trap
            if not work and pending_tables:
                for ins in pending_tables:
                    tab = self._jump_table(func, ins)
                    if tab:
                        ins.table = tab
                        for t in tab[1]:
                            func.leaders.add(t)
                            work.append(t)
                pending_tables = []
        return func

    def discover(self, seeds, blacklist=()):
        # An address whose first four bytes are themselves relocated holds an
        # absolute pointer: that is data (typically a jump table), not code.
        data_like = {e for e in seeds if e in self.img.relocs}
        self.notes += [f'ignored seed {e:#x}: starts with a relocated dword' for e in sorted(data_like)]
        self.blacklist = set(blacklist) | data_like
        self.entries = set(seeds) - self.blacklist
        while True:
            self._decode_all()
            added = self._pointer_entries() - self.entries - self.blacklist
            if not added:
                break
            self.entries |= added
        # Values that turned out to be jump tables are not functions.
        for e in list(self.entries):
            if e in self.table_vas or e in self.table_slots:
                self.entries.discard(e)
                self.functions.pop(e, None)
                self.notes.append(f'dropped entry {e:#x}: jump table')
        return self.functions

    def _decode_all(self):
        changed = True
        while changed:
            changed = False
            for e in sorted(self.entries):
                if e in self.functions:
                    continue
                f = self.build_function(e)
                self.functions[e] = f
                for t in f.calls:
                    if t not in self.entries and t not in self.blacklist and self.img.in_text(t):
                        self.entries.add(t)
                        changed = True
            # A function decoded before a later entry was known may have
            # followed a jmp into what is now an entry; rebuild those so the
            # jmp becomes a tail call and the code is not duplicated.
            for e, f in list(self.functions.items()):
                if any(t in self.entries and t != e for t in self._local_jump_targets(f)):
                    del self.functions[e]
                    changed = True

    def code_pointer_relocs(self):
        """Classify every relocated dword whose value points into the code."""
        owner = {}
        for f in self.functions.values():
            for ins in f.insns.values():
                for b in range(ins.addr, ins.next):
                    owner[b] = ins
        out = []
        for r in sorted(self.img.relocs):
            v = self.img.u32(r)
            if not self.img.in_text(v):
                continue
            if r in self.table_slots:
                kind = 'table-slot'
            elif not self.img.in_text(r):
                kind = 'data'
            elif r in owner:
                ins = owner[r]
                imm = [op for op in ins.ops if op.type == X.X86_OP_IMM and (op.imm & 0xffffffff) == v]
                kind = 'insn-imm' if imm and ins.flow not in ('call', 'jmp', 'jcc') else 'insn-disp'
            else:
                kind = 'text-unowned'
            out.append((r, v, kind))
        return out

    def _pointer_entries(self):
        found = set()
        relocs = self.img.relocs
        for r, v, kind in self.code_pointer_relocs():
            if v in self.table_vas or v in self.table_slots or v in relocs:
                continue
            if kind == 'text-unowned' and (r - 4 in relocs or r + 4 in relocs):
                # Contiguous pointers inside the code section are almost
                # certainly a jump table of code not decoded yet; its targets
                # are case labels, not functions.
                continue
            if kind in ('data', 'insn-imm', 'text-unowned'):
                found.add(v)
        return found

    def _local_jump_targets(self, f):
        for ins in f.insns.values():
            if ins.flow == 'jmp':
                t = ins.targets[0]
                if t not in f.tailcalls:
                    yield t
