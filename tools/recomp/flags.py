"""Interprocedural EFLAGS liveness.

Every flag-producing instruction only materialises the flags that some later
instruction can actually read. Flags are tracked per bit (CF, PF, AF, ZF, SF,
OF). DF is always materialised (cld/std are rare).

A call defines all flags (the callee leaves them behind) and reads the flags
the callee reads on entry. A ret reads the flags that any caller reads after
the call (ret_live), which is how the Watcom x87 helper at 0x444050 can
return a result in CF.
"""
from capstone import x86 as X

CF, PF, AF, ZF, SF, OF = 1, 2, 4, 8, 16, 32
ALL = CF | PF | AF | ZF | SF | OF
ARITH = ALL
NOT_CF = ALL & ~CF

CC_READS = {
    'o': OF, 'no': OF, 'b': CF, 'ae': CF, 'e': ZF, 'ne': ZF, 'be': CF | ZF, 'a': CF | ZF,
    's': SF, 'ns': SF, 'p': PF, 'np': PF, 'l': SF | OF, 'ge': SF | OF,
    'le': ZF | SF | OF, 'g': ZF | SF | OF,
}


def cc_of(mnem):
    """Condition code suffix of a jcc/setcc mnemonic, or None."""
    if mnem.startswith('set'):
        return mnem[3:]
    if mnem.startswith('j') and mnem[1:] in CC_READS:
        return mnem[1:]
    return None


def shift_count_is_imm(ins):
    return len(ins.ops) < 2 or ins.ops[1].type == X.X86_OP_IMM


def use_def(ins):
    """Return (use, definite_def, conditional_def) flag masks."""
    m = ins.mnem
    cc = cc_of(m)
    if cc is not None:
        return CC_READS[cc], 0, 0
    if m in ('add', 'sub', 'cmp', 'neg', 'and', 'or', 'xor', 'test'):
        return 0, ALL, 0
    if m in ('adc', 'sbb'):
        return CF, ALL, 0
    if m in ('inc', 'dec'):
        return 0, NOT_CF, 0
    if m in ('shl', 'sal', 'shr', 'sar'):
        if shift_count_is_imm(ins):
            n = ins.ops[1].imm & 31 if len(ins.ops) > 1 else 1
            return (0, ALL, 0) if n else (0, 0, 0)
        return 0, 0, ALL
    if m in ('rol', 'ror'):
        if shift_count_is_imm(ins):
            n = ins.ops[1].imm & 31 if len(ins.ops) > 1 else 1
            return (0, CF | OF, 0) if n else (0, 0, 0)
        return 0, 0, CF | OF
    if m in ('rcl', 'rcr'):
        if shift_count_is_imm(ins):
            n = ins.ops[1].imm & 31 if len(ins.ops) > 1 else 1
            return (CF, CF | OF, 0) if n else (0, 0, 0)
        return CF, 0, CF | OF
    if m in ('mul', 'imul', 'div', 'idiv', 'aam', 'aad', 'daa', 'das', 'aaa', 'aas'):
        return (CF | AF if m in ('daa', 'das', 'aaa', 'aas') else 0), ALL, 0
    if m in ('cmpsb', 'cmpsw', 'cmpsd', 'scasb', 'scasw', 'scasd'):
        if ins.prefix:
            return 0, 0, ALL
        return 0, ALL, 0
    if m in ('loope', 'loopne'):
        return ZF, 0, 0
    if m == 'sahf':
        return 0, CF | PF | AF | ZF | SF, 0
    if m == 'lahf':
        return CF | PF | AF | ZF | SF, 0, 0
    if m == 'pushfd':
        return ALL, 0, 0
    if m == 'popfd':
        return 0, ALL, 0
    if m in ('clc', 'stc'):
        return 0, CF, 0
    if m == 'cmc':
        return CF, CF, 0
    if m in ('verr', 'verw'):
        return 0, ZF, 0
    if m in ('bt', 'bts', 'btr', 'btc'):
        return 0, ALL, 0
    if m in ('bsf', 'bsr'):
        return 0, ALL, 0
    return 0, 0, 0


class Liveness:
    def __init__(self, program):
        self.prog = program
        self.funcs = program.functions
        self.entry_live = {e: 0 for e in self.funcs}
        self.ret_live = {e: 0 for e in self.funcs}
        self.live_out = {}            # (func entry, insn addr) -> mask
        self.warnings = []
        self.pointer_entries = set()

    def succs(self, f, ins):
        if ins.flow in ('seq', 'call', 'calli'):
            return [ins.next] if ins.next in f.insns else []
        if ins.flow == 'jcc':
            return [t for t in ins.targets if t in f.insns]
        if ins.flow == 'jmp':
            t = ins.targets[0]
            return [] if t in f.tailcalls else [t]
        if ins.flow == 'jmpi' and ins.table:
            return [t for t in dict.fromkeys(ins.table[1])]
        return []

    def local_use_def(self, f, ins):
        use, d, cd = use_def(ins)
        if ins.flow == 'call':
            t = ins.targets[0]
            use |= self.entry_live.get(t, 0)
            d = ALL
        elif ins.flow == 'calli':
            d = ALL
        elif ins.flow == 'ret':
            use |= self.ret_live[f.entry]
        elif ins.flow == 'jmp' and ins.targets[0] in f.tailcalls:
            use |= self.entry_live.get(ins.targets[0], 0)
        return use, d, cd

    def solve_function(self, f):
        order = sorted(f.insns, reverse=True)
        live_in = {a: 0 for a in order}
        live_out = {a: 0 for a in order}
        ud = {a: self.local_use_def(f, f.insns[a]) for a in order}
        sc = {a: self.succs(f, f.insns[a]) for a in order}
        changed = True
        while changed:
            changed = False
            for a in order:
                out = 0
                for s in sc[a]:
                    out |= live_in.get(s, 0)
                use, d, _ = ud[a]
                inn = use | (out & ~d)
                if out != live_out[a] or inn != live_in[a]:
                    live_out[a], live_in[a] = out, inn
                    changed = True
        return live_in, live_out

    def solve(self):
        for _ in range(50):
            changed = False
            for e, f in self.funcs.items():
                live_in, live_out = self.solve_function(f)
                if live_in[e] != self.entry_live[e]:
                    self.entry_live[e] = live_in[e]
                    changed = True
                for a, ins in f.insns.items():
                    self.live_out[(e, a)] = live_out[a]
                    if ins.flow == 'call':
                        t = ins.targets[0]
                        if t in self.ret_live and live_out[a] & ~self.ret_live[t]:
                            self.ret_live[t] |= live_out[a]
                            changed = True
                    elif ins.flow == 'jmp' and ins.targets[0] in f.tailcalls:
                        t = ins.targets[0]
                        if t in self.ret_live and self.ret_live[e] & ~self.ret_live[t]:
                            self.ret_live[t] |= self.ret_live[e]
                            changed = True
                    elif ins.flow == 'calli' and live_out[a]:
                        self.warnings.append(f'{a:#x}: flags {live_out[a]:#x} read after indirect call')
            if not changed:
                break
        else:
            raise RuntimeError('flag liveness did not converge')
        for e, live in self.entry_live.items():
            if live:
                self.warnings.append(f'function {e:#x} reads flags {live:#x} on entry')
        return self
