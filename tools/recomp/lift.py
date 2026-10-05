"""Translate decoded x86 instructions into C.

The generated code targets src/runtime/rt_cpu.h: a Cpu struct holding the
register file and flags, and R8/R16/R32 (W8/...) accessors on the flat guest
memory. Every guest function becomes

    uint32_t f_XXXXXXXX(Cpu *c)

which returns the return address its final 'ret' popped. A call site compares
that with its own return address. A mismatch means the guest returned
somewhere else (a pushed continuation, longjmp-style unwinding); the caller
then either continues at a local label for that address or propagates the
address to its own caller, which mirrors what the CPU does.
"""
from capstone import x86 as X

from . import flags as F

MASK = {1: 0xff, 2: 0xffff, 4: 0xffffffff, 8: 0xffffffffffffffff}
BITS = {1: 8, 2: 16, 4: 32}
SIGNED = {1: 'int8_t', 2: 'int16_t', 4: 'int32_t'}
UNSIGNED = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}
RW = {1: '8', 2: '16', 4: '32', 8: '64'}

CC_EXPR = {
    'o': 'c->of', 'no': '!c->of', 'b': 'c->cf', 'ae': '!c->cf', 'e': 'c->zf', 'ne': '!c->zf',
    'be': '(c->cf | c->zf)', 'a': '!(c->cf | c->zf)', 's': 'c->sf', 'ns': '!c->sf',
    'p': 'c->pf', 'np': '!c->pf', 'l': '(c->sf != c->of)', 'ge': '(c->sf == c->of)',
    'le': '(c->zf | (c->sf != c->of))', 'g': '!(c->zf | (c->sf != c->of))',
}


class LiftError(Exception):
    pass


def hx(v):
    return f'0x{v & 0xffffffff:08x}u'


class Lifter:
    def __init__(self, program, liveness, host_symbols):
        self.prog = program
        self.live = liveness
        self.host = host_symbols            # IAT slot -> host function symbol
        self.md = program.md
        self.unsupported = {}
        # Function entry -> index into rt_cov[] (per-function call counters).
        self.coverage_index = {e: i for i, e in enumerate(sorted(program.functions))}

    # ----------------------------------------------------------- operands
    def reg(self, r):
        name = self.md.reg_name(r)
        if name is None:
            raise LiftError(f'bad register {r}')
        if name.startswith('st('):
            raise LiftError('x87 register used as integer operand')
        return f'c->{name}'

    def ea(self, op):
        m = op.mem
        parts = []
        if m.base:
            parts.append(self.reg(m.base))
        if m.index:
            idx = self.reg(m.index)
            parts.append(f'{idx} * {m.scale}u' if m.scale != 1 else idx)
        disp = m.disp & 0xffffffff
        if disp or not parts:
            parts.append(hx(disp))
        expr = ' + '.join(parts)
        if m.segment == X.X86_REG_FS:
            expr = f'c->fs_base + {expr}'
        elif m.segment == X.X86_REG_GS:
            raise LiftError('gs segment')
        return f'(uint32_t)({expr})'

    def rd(self, op, eav=None):
        """Read operand; returns a C expression (zero-extended uint32_t)."""
        if op.type == X.X86_OP_REG:
            return self.reg(op.reg)
        if op.type == X.X86_OP_IMM:
            return hx(op.imm & MASK[op.size])
        if op.type == X.X86_OP_MEM:
            return f'R{RW[op.size]}({eav or self.ea(op)})'
        raise LiftError('operand type')

    def wr(self, op, value, eav=None):
        if op.type == X.X86_OP_REG:
            return f'{self.reg(op.reg)} = ({UNSIGNED[op.size]})({value});'
        if op.type == X.X86_OP_MEM:
            return f'W{RW[op.size]}({eav or self.ea(op)}, ({UNSIGNED.get(op.size, "uint64_t")})({value}));'
        raise LiftError('write to non-lvalue')

    # -------------------------------------------------------------- flags
    def flag_stmts(self, kind, n, a, b, r, need, cin='0'):
        """Statements that materialise the needed flags of an operation.

        a, b, r are C expressions (already masked to n bytes) naming the
        operands and result. kind selects the CF/OF/AF formulas.
        """
        if not need:
            return []
        bits = BITS[n]
        sb = bits - 1
        out = []
        if need & F.CF:
            cf = {
                'add': f'(({r}) < ({a}))',
                'adc': f'(uint32_t)((((uint64_t)({a}) + ({b}) + ({cin})) >> {bits}) & 1)',
                'sub': f'(({a}) < ({b}))',
                'sbb': f'(uint32_t)((((uint64_t)({a}) - ({b}) - ({cin})) >> {bits}) & 1)',
                'logic': '0', 'neg': f'(({a}) != 0)',
            }.get(kind)
            if cf is not None:
                out.append(f'c->cf = {cf};')
        if need & F.OF:
            of = {
                'add': f'(((({a}) ^ ({r})) & (({b}) ^ ({r}))) >> {sb}) & 1',
                'adc': f'(((({a}) ^ ({r})) & (({b}) ^ ({r}))) >> {sb}) & 1',
                'sub': f'(((({a}) ^ ({b})) & (({a}) ^ ({r}))) >> {sb}) & 1',
                'sbb': f'(((({a}) ^ ({b})) & (({a}) ^ ({r}))) >> {sb}) & 1',
                'logic': '0', 'neg': f'(({r}) == {hx(1 << sb)})',
                'inc': f'(({r}) == {hx(1 << sb)})', 'dec': f'(({a}) == {hx(1 << sb)})',
            }[kind]
            out.append(f'c->of = {of};')
        if need & F.AF:
            af = {'logic': '0', 'inc': f'((({r}) & 0xf) == 0)', 'dec': f'((({r}) & 0xf) == 0xf)',
                  'neg': f'(((0 ^ ({a}) ^ ({r})) >> 4) & 1)'}.get(kind, f'((({a}) ^ ({b}) ^ ({r})) >> 4) & 1')
            out.append(f'c->af = {af};')
        if need & F.ZF:
            out.append(f'c->zf = (({r}) == 0);')
        if need & F.SF:
            out.append(f'c->sf = (({r}) >> {sb}) & 1;')
        if need & F.PF:
            out.append(f'c->pf = PARITY({r});')
        return out

    # -------------------------------------------------------- functions
    def function(self, f):
        """Return C source for one guest function."""
        self.f = f
        self.conts = self.continuations(f)
        self.preds = self.pred_count(f)
        self.fused = {}
        order = sorted(f.insns)
        labels = {f.entry} | self.conts
        for ins in f.insns.values():
            labels.update(self.prog_local_targets(f, ins))
        gaps = {}
        for i, a in enumerate(order):
            ins = f.insns[a]
            if ins.flow in ('seq', 'call', 'calli', 'jcc'):
                nxt = order[i + 1] if i + 1 < len(order) else None
                if nxt != ins.next:
                    gaps[a] = ins.next
                    if ins.next in f.insns:
                        labels.add(ins.next)
        L = [f'uint32_t f_{f.entry:08x}(Cpu *c)', '{',
             '    uint32_t ra; uint32_t ft_a = 0, ft_b = 0, ft_r = 0;',
             '    (void)ra; (void)ft_a; (void)ft_b; (void)ft_r;']
        if self.coverage_index is not None:
            L.append(f'    RT_COV({self.coverage_index[f.entry]});')
        if order[0] != f.entry:
            L.append(f'    goto L_{f.entry:08x};')
        for a in order:
            ins = f.insns[a]
            if a in labels:
                L.append(f'L_{a:08x}:')
            try:
                body = self.insn(f, ins)
            except LiftError as e:
                key = f'{ins.mnem}: {e}'
                self.unsupported[key] = self.unsupported.get(key, 0) + 1
                body = [f'rt_trap(c, {hx(a)}, "unsupported instruction");']
            text = f'{ins.cs.mnemonic} {ins.cs.op_str}'.strip().replace('*/', '* /')
            L.append(f'    /* {a:08x}: {text} */')
            L.extend('    ' + b for b in body)
            if a in gaps:
                t = gaps[a]
                if t in f.insns:
                    L.append(f'    goto L_{t:08x};')
                else:
                    L.append(f'    rt_trap(c, {hx(t)}, "execution left decoded code");')
        L.append('    rt_trap(c, 0, "unreachable end of function"); return 0;')
        L.append('}')
        return '\n'.join(L)

    def prog_local_targets(self, f, ins):
        if ins.flow == 'jcc':
            return [t for t in ins.targets if t in f.insns]
        if ins.flow == 'jmp' and ins.targets[0] not in f.tailcalls:
            return [ins.targets[0]]
        if ins.flow == 'jmpi' and ins.table:
            return list(ins.table[1])
        return []

    def pred_count(self, f):
        preds = {a: 0 for a in f.insns}
        for ins in f.insns.values():
            for s in self.live.succs(f, ins):
                if s in preds:
                    preds[s] += 1
        preds[f.entry] = preds.get(f.entry, 0) + 1
        for a in self.conts:
            preds[a] = preds.get(a, 0) + 1
        return preds

    def continuations(self, f):
        """Code addresses this function pushes that are also local labels."""
        out = set()
        for ins in f.insns.values():
            if ins.mnem == 'push' and ins.ops and ins.ops[0].type == X.X86_OP_IMM:
                v = ins.ops[0].imm & 0xffffffff
                if v in f.insns and v != f.entry:
                    out.add(v)
        return out

    def mismatch(self, expected):
        """Code run when a callee returned to an unexpected address."""
        tests = ' '.join(f'if (ra == {hx(t)}) goto L_{t:08x};' for t in sorted(self.conts))
        return f'if (ra != {hx(expected)}) {{ {tests} return ra; }}'

    def need(self, f, ins):
        _, d, cd = F.use_def(ins)
        return (d | cd) & self.live.live_out.get((f.entry, ins.addr), F.ALL)

    # ------------------------------------------------------------ fusion
    FUSE_ALL = {'cmp'}
    FUSE_LOGIC = {'test', 'and', 'or', 'xor'}
    FUSE_ZS = {'add', 'sub', 'inc', 'dec', 'neg', 'sar', 'shl', 'shr'}

    def fusable_cc(self, setter, cc):
        if setter.mnem in self.FUSE_ALL:
            return cc in ('e', 'ne', 'l', 'ge', 'le', 'g', 'b', 'ae', 'be', 'a', 's', 'ns')
        if setter.mnem in self.FUSE_LOGIC:
            return cc in ('e', 'ne', 's', 'ns', 'l', 'ge', 'le', 'g', 'be', 'a')
        if setter.mnem in self.FUSE_ZS:
            return cc in ('e', 'ne', 's', 'ns')
        return False

    def fusion_chain(self, f, setter):
        """Readers right after setter that can use its temporaries.

        Returns (chain, need): the fused readers and the flags the setter
        must still materialise for everything else.
        """
        if setter.mnem not in self.FUSE_ALL | self.FUSE_LOGIC | self.FUSE_ZS:
            return [], 0
        if setter.mnem in ('sar', 'shl', 'shr') and not F.shift_count_is_imm(setter):
            return [], 0
        _, sdef, _ = F.use_def(setter)
        if not sdef:
            return [], 0               # e.g. a shift by 0 leaves the old flags
        chain, extra, prev = [], 0, setter
        while True:
            nxt = f.insns.get(prev.next)
            if nxt is None or self.preds.get(nxt.addr, 0) != 1 or nxt.addr in self.conts:
                break
            cc = F.cc_of(nxt.mnem)
            if cc is None or nxt.flow not in ('jcc', 'seq'):
                break
            if self.fusable_cc(setter, cc):
                chain.append(nxt)
            else:
                extra |= F.CC_READS[cc]
            prev = nxt
        if not chain:
            return [], 0
        # Flags read where the chain is left: after the last reader and at
        # the target of every branch taken inside the chain.
        live_after = self.live.live_out.get((f.entry, prev.addr), F.ALL)
        r = setter
        while r is not prev:
            r = f.insns[r.next]
            if r.flow == 'jcc' and r is not prev:
                live_after |= self.live_in(f, r.targets[0])
        return chain, (sdef & live_after) | (extra & sdef)

    def live_in(self, f, addr):
        ins = f.insns.get(addr)
        if ins is None:
            return F.ALL
        use, d, _ = self.live.local_use_def(f, ins)
        return use | (self.live.live_out.get((f.entry, addr), F.ALL) & ~d)

    def fused_cond(self, setter, cc):
        n = setter.ops[0].size
        s, u = SIGNED[n], UNSIGNED[n]
        m = setter.mnem
        if m == 'cmp':
            a, b = f'({s})ft_a', f'({s})ft_b'
            ua, ub = f'({u})ft_a', f'({u})ft_b'
            return {'e': f'{ua} == {ub}', 'ne': f'{ua} != {ub}', 'l': f'{a} < {b}',
                    'ge': f'{a} >= {b}', 'le': f'{a} <= {b}', 'g': f'{a} > {b}',
                    'b': f'{ua} < {ub}', 'ae': f'{ua} >= {ub}', 'be': f'{ua} <= {ub}',
                    'a': f'{ua} > {ub}', 's': f'({s})({u})(ft_a - ft_b) < 0',
                    'ns': f'({s})({u})(ft_a - ft_b) >= 0'}[cc]
        r, sr = f'({u})ft_r', f'({s})ft_r'
        return {'e': f'{r} == 0', 'ne': f'{r} != 0', 's': f'{sr} < 0', 'ns': f'{sr} >= 0',
                'l': f'{sr} < 0', 'ge': f'{sr} >= 0', 'le': f'{sr} <= 0', 'g': f'{sr} > 0',
                'be': f'{r} == 0', 'a': f'{r} != 0'}[cc]

    # -------------------------------------------------------- dispatcher
    def insn(self, f, ins):
        m = ins.mnem
        if ins.flow == 'trap':
            return [f'rt_trap(c, {hx(ins.addr)}, "{ins.cs.mnemonic} {ins.cs.op_str}");']
        handler = getattr(self, 'i_' + m, None)
        if m.startswith('j') and m not in ('jmp', 'jecxz') or m.startswith('set'):
            handler = self.i_cc
        if m.startswith('f') or m in ('wait',):
            handler = self.i_fpu
        if handler is None:
            raise LiftError(f'no handler for {m}')
        cond = self.fused.get(ins.addr)
        if cond is not None and F.cc_of(m):
            return self.i_cc(f, ins, cond)
        return handler(f, ins)

    # ----------------------------------------------------------- control
    def i_cc(self, f, ins, fused_setter=None):
        cc = F.cc_of(ins.mnem)
        if fused_setter is not None:
            cond = self.fused_cond(fused_setter, cc)
        else:
            cond = CC_EXPR[cc]
        if ins.mnem.startswith('set'):
            op = ins.ops[0]
            return [self.wr(op, f'({cond}) ? 1 : 0')]
        return [f'if ({cond}) {self.goto(ins, ins.targets[0])}']

    @staticmethod
    def goto(ins, t):
        """Jump to a local label. Backward jumps close loops; there another
        guest thread waiting for the global lock gets a turn (spin-waits on
        flags set by the sound timer thread depend on it)."""
        if t <= ins.addr:
            return f'{{ RT_POLL(); goto L_{t:08x}; }}'
        return f'goto L_{t:08x};'

    def i_jmp(self, f, ins):
        if ins.flow == 'jmp':
            t = ins.targets[0]
            if t in f.tailcalls:
                return [f'return f_{t:08x}(c);']
            return [self.goto(ins, t)]
        op = ins.ops[0]
        if ins.iat is not None:
            return [f'return {self.host[ins.iat]}(c);']
        if ins.table:
            table, tgts = ins.table
            src = self.rd(op)
            cases = ' '.join(f'case {hx(t)}: goto L_{t:08x};' for t in dict.fromkeys(tgts))
            return [f'{{ uint32_t t = {src}; switch (t) {{ {cases} default: return rt_jump_indirect(c, t); }} }}']
        return [f'return rt_jump_indirect(c, {self.rd(op)});']

    def i_call(self, f, ins):
        ret = ins.next
        if ins.flow == 'call':
            t = ins.targets[0]
            if t in self.prog.functions:
                callee = f'f_{t:08x}(c)'
            else:
                callee = f'rt_call_indirect(c, {hx(t)})'
            return [f'PUSH32({hx(ret)});', f'ra = {callee};', self.mismatch(ret)]
        op = ins.ops[0]
        if ins.iat is not None:
            return [f'PUSH32({hx(ret)});', f'ra = {self.host[ins.iat]}(c);', self.mismatch(ret)]
        return [f'{{ uint32_t t = {self.rd(op)}; PUSH32({hx(ret)}); ra = rt_call_indirect(c, t); }}',
                self.mismatch(ret)]

    def i_ret(self, f, ins):
        n = ins.ops[0].imm if ins.ops else 0
        out = ['ra = R32(c->esp);', f'c->esp += {4 + n}u;']
        if self.conts:
            out.append(' '.join(f'if (ra == {hx(t)}) goto L_{t:08x};' for t in sorted(self.conts)))
        out.append('return ra;')
        return out

    def _loop(self, f, ins, cond):
        t = ins.targets[0]
        return [f'if ({cond}) {self.goto(ins, t)}']

    def i_loop(self, f, ins):
        return self._loop(f, ins, '--c->ecx != 0')

    def i_loope(self, f, ins):
        return self._loop(f, ins, '--c->ecx != 0 && c->zf')

    def i_loopne(self, f, ins):
        return self._loop(f, ins, '--c->ecx != 0 && !c->zf')

    def i_jecxz(self, f, ins):
        return self._loop(f, ins, 'c->ecx == 0')

    # -------------------------------------------------------------- moves
    SEGS = {'cs': 0x23, 'ds': 0x2b, 'es': 0x2b, 'ss': 0x2b, 'fs': 0x53, 'gs': 0x2b}

    def is_seg(self, op):
        return op.type == X.X86_OP_REG and self.md.reg_name(op.reg) in self.SEGS

    def i_mov(self, f, ins):
        d, s = ins.ops
        if self.is_seg(d):
            return ['/* segment register load ignored in the flat model */']
        if self.is_seg(s):
            return [self.wr(d, hx(self.SEGS[self.md.reg_name(s.reg)]))]
        return [self.wr(d, self.rd(s))]

    def i_movzx(self, f, ins):
        d, s = ins.ops
        return [self.wr(d, self.rd(s))]

    def i_movsx(self, f, ins):
        d, s = ins.ops
        return [self.wr(d, f'(uint32_t)(int32_t)({SIGNED[s.size]})({self.rd(s)})')]

    def i_lea(self, f, ins):
        d, s = ins.ops
        return [self.wr(d, self.ea(s))]

    def i_xchg(self, f, ins):
        a, b = ins.ops
        out = ['{']
        ea_a = ea_b = None
        if a.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(a)};')
            ea_a = 'ea'
        if b.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(b)};')
            ea_b = 'ea'
        out.append(f'uint32_t t1 = {self.rd(a, ea_a)}, t2 = {self.rd(b, ea_b)};')
        out.append(self.wr(a, 't2', ea_a))
        out.append(self.wr(b, 't1', ea_b))
        out.append('}')
        return [' '.join(out)]

    def i_push(self, f, ins):
        op = ins.ops[0]
        if self.is_seg(op):
            return [f'PUSH32({hx(self.SEGS[self.md.reg_name(op.reg)])});']
        if op.size == 2:
            return [f'{{ uint32_t t = {self.rd(op)}; c->esp -= 2; W16(c->esp, (uint16_t)t); }}']
        if op.type == X.X86_OP_IMM:
            return [f'PUSH32({hx(op.imm)});']
        return [f'PUSH32({self.rd(op)});']

    def i_pop(self, f, ins):
        op = ins.ops[0]
        if self.is_seg(op):
            return ['c->esp += 4; /* segment register pop ignored */']
        if op.size == 2:
            return [f'{{ uint32_t t = R16(c->esp); c->esp += 2; {self.wr(op, "t")} }}']
        if op.type == X.X86_OP_MEM:
            # The address is computed after esp has been incremented.
            return [f'{{ uint32_t t = R32(c->esp); c->esp += 4; {self.wr(op, "t")} }}']
        return [f'{{ uint32_t t = R32(c->esp); c->esp += 4; {self.wr(op, "t")} }}']

    def i_pushal(self, f, ins):
        return ['{ uint32_t t = c->esp; PUSH32(c->eax); PUSH32(c->ecx); PUSH32(c->edx); PUSH32(c->ebx);'
                ' PUSH32(t); PUSH32(c->ebp); PUSH32(c->esi); PUSH32(c->edi); }']

    def i_popal(self, f, ins):
        return ['c->edi = R32(c->esp); c->esi = R32(c->esp + 4); c->ebp = R32(c->esp + 8);'
                ' c->ebx = R32(c->esp + 16); c->edx = R32(c->esp + 20); c->ecx = R32(c->esp + 24);'
                ' c->eax = R32(c->esp + 28); c->esp += 32;']

    def i_pushfd(self, f, ins):
        return ['PUSH32(rt_get_eflags(c));']

    def i_popfd(self, f, ins):
        return ['rt_set_eflags(c, R32(c->esp)); c->esp += 4;']

    def i_leave(self, f, ins):
        return ['c->esp = c->ebp; c->ebp = R32(c->esp); c->esp += 4;']

    def i_cwde(self, f, ins):
        return ['c->eax = (uint32_t)(int32_t)(int16_t)c->ax;']

    def i_cbw(self, f, ins):
        return ['c->ax = (uint16_t)(int16_t)(int8_t)c->al;']

    def i_cdq(self, f, ins):
        return ['c->edx = (uint32_t)((int32_t)c->eax >> 31);']

    def i_cwd(self, f, ins):
        return ['c->dx = (uint16_t)((int16_t)c->ax >> 15);']

    def i_nop(self, f, ins):
        return []

    def i_xlatb(self, f, ins):
        return ['c->al = R8((uint32_t)(c->ebx + c->al));']

    def i_cld(self, f, ins):
        return ['c->df = 0;']

    def i_std(self, f, ins):
        return ['c->df = 1;']

    def i_clc(self, f, ins):
        return ['c->cf = 0;']

    def i_stc(self, f, ins):
        return ['c->cf = 1;']

    def i_cmc(self, f, ins):
        return ['c->cf ^= 1;']

    def i_sahf(self, f, ins):
        return ['c->sf = (c->ah >> 7) & 1; c->zf = (c->ah >> 6) & 1; c->af = (c->ah >> 4) & 1;'
                ' c->pf = (c->ah >> 2) & 1; c->cf = c->ah & 1;']

    def i_lahf(self, f, ins):
        return ['c->ah = (uint8_t)((c->sf << 7) | (c->zf << 6) | (c->af << 4) | (c->pf << 2) | 2 | c->cf);']

    def i_verr(self, f, ins):
        return ['c->zf = 1; /* every selector is readable in the flat model */']

    def i_les(self, f, ins):
        # Far pointer load (Watcom printf's %n path): the offset goes to the
        # register; the selector half is ignored in the flat model, like a
        # 'mov es, r'.
        d, s = ins.ops
        return [self.wr(d, f'R32({self.ea(s)})')]

    i_lds = i_les

    # --------------------------------------------------------- arithmetic
    def _binop(self, f, ins, kind, expr, store=True, cin=None):
        d, s = ins.ops
        n = d.size
        mask = hx(MASK[n])
        need = self.need(f, ins)
        chain, fneed = self.fusion_chain(f, ins)
        out = ['{']
        eav = None
        if d.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(d)};')
            eav = 'ea'
        b = hx(s.imm & MASK[n]) if s.type == X.X86_OP_IMM else self.rd(s)
        out.append(f'uint32_t a = {self.rd(d, eav)}, b = {b};')
        if cin is not None:
            out.append(f'uint32_t ci = {cin};')
        out.append(f'uint32_t r = ({expr}) & {mask};')
        if store:
            out.append(self.wr(d, 'r', eav))
        if chain:
            need = fneed
            for r in chain:
                self.fused[r.addr] = ins
            out.append('ft_a = a; ft_b = b; ft_r = r;')
        out += self.flag_stmts(kind, n, 'a', 'b', 'r', need, cin='ci' if cin else '0')
        out.append('}')
        return [' '.join(out)]

    def i_add(self, f, ins):
        return self._binop(f, ins, 'add', 'a + b')

    def i_sub(self, f, ins):
        return self._binop(f, ins, 'sub', 'a - b')

    def i_cmp(self, f, ins):
        return self._binop(f, ins, 'sub', 'a - b', store=False)

    def i_adc(self, f, ins):
        return self._binop(f, ins, 'adc', 'a + b + ci', cin='c->cf')

    def i_sbb(self, f, ins):
        return self._binop(f, ins, 'sbb', 'a - b - ci', cin='c->cf')

    def i_and(self, f, ins):
        return self._binop(f, ins, 'logic', 'a & b')

    def i_or(self, f, ins):
        return self._binop(f, ins, 'logic', 'a | b')

    def i_xor(self, f, ins):
        d, s = ins.ops
        if d.type == X.X86_OP_REG and s.type == X.X86_OP_REG and d.reg == s.reg:
            need = self.need(f, ins)
            chain, fneed = self.fusion_chain(f, ins)
            out = [self.wr(d, '0')]
            if chain:
                need = fneed
                for r in chain:
                    self.fused[r.addr] = ins
                out.append('ft_a = 0; ft_b = 0; ft_r = 0;')
            out += self.flag_stmts('logic', d.size, '0u', '0u', '0u', need)
            return [' '.join(out)]
        return self._binop(f, ins, 'logic', 'a ^ b')

    def i_test(self, f, ins):
        return self._binop(f, ins, 'logic', 'a & b', store=False)

    def _unop(self, f, ins, kind, expr):
        d = ins.ops[0]
        n = d.size
        need = self.need(f, ins)
        chain, fneed = self.fusion_chain(f, ins)
        out = ['{']
        eav = None
        if d.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(d)};')
            eav = 'ea'
        out.append(f'uint32_t a = {self.rd(d, eav)};')
        out.append(f'uint32_t r = ({expr}) & {hx(MASK[n])};')
        out.append(self.wr(d, 'r', eav))
        if chain:
            need = fneed
            for r in chain:
                self.fused[r.addr] = ins
            out.append('ft_a = a; ft_b = 0; ft_r = r;')
        out += self.flag_stmts(kind, n, 'a', '1u' if kind in ('inc', 'dec') else '0u', 'r', need)
        out.append('}')
        return [' '.join(out)]

    def i_inc(self, f, ins):
        return self._unop(f, ins, 'inc', 'a + 1')

    def i_dec(self, f, ins):
        return self._unop(f, ins, 'dec', 'a - 1')

    def i_neg(self, f, ins):
        return self._unop(f, ins, 'neg', '0u - a')

    def i_not(self, f, ins):
        d = ins.ops[0]
        eav = None
        out = ['{']
        if d.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(d)};')
            eav = 'ea'
        out.append(self.wr(d, f'~{self.rd(d, eav)}', eav))
        out.append('}')
        return [' '.join(out)]

    # ------------------------------------------------------------- shifts
    def _shift(self, f, ins, m):
        d = ins.ops[0]
        n = d.size
        bits = BITS[n]
        mask = hx(MASK[n])
        need = self.need(f, ins)
        imm = F.shift_count_is_imm(ins)
        if len(ins.ops) > 1:
            cnt = hx(ins.ops[1].imm & 31) if imm else '(c->cl & 31u)'
        else:
            cnt = '1u'
        chain, fneed = self.fusion_chain(f, ins) if imm else ([], 0)
        if chain:
            need = fneed
            for r in chain:
                self.fused[r.addr] = ins
        out = ['{']
        eav = None
        if d.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(d)};')
            eav = 'ea'
        out.append(f'uint32_t a = {self.rd(d, eav)}, k = {cnt}, r = a;')
        sb = bits - 1
        sat = f'(k >= {bits} ? {bits} : k)'
        body = []
        if m in ('shl', 'sal'):
            body.append(f'r = (uint32_t)(((uint64_t)a << k) & {mask});')
            fl = []
            if need & F.CF:
                fl.append(f'c->cf = (uint32_t)(((uint64_t)a >> ({bits} - {sat})) & 1);')
            if need & F.OF:
                fl.append(f'c->of = ((r >> {sb}) & 1) ^ (uint32_t)(((uint64_t)a >> ({bits} - {sat})) & 1);')
        elif m == 'shr':
            body.append(f'r = (uint32_t)((uint64_t)a >> k);')
            fl = []
            if need & F.CF:
                fl.append(f'c->cf = (uint32_t)(((uint64_t)a >> (k - 1)) & 1);')
            if need & F.OF:
                fl.append(f'c->of = (a >> {sb}) & 1;')
        elif m == 'sar':
            body.append(f'r = (uint32_t)((int64_t)({SIGNED[n]})a >> k) & {mask};')
            fl = []
            if need & F.CF:
                fl.append(f'c->cf = (uint32_t)(((int64_t)({SIGNED[n]})a >> (k - 1)) & 1);')
            if need & F.OF:
                fl.append('c->of = 0;')
        else:
            raise LiftError(m)
        if need & F.AF:
            fl.append('c->af = 0;')
        if need & F.ZF:
            fl.append('c->zf = (r == 0);')
        if need & F.SF:
            fl.append(f'c->sf = (r >> {sb}) & 1;')
        if need & F.PF:
            fl.append('c->pf = PARITY(r);')
        cond_open = '' if imm else 'if (k) {'
        cond_close = '' if imm else '}'
        out.append(cond_open)
        out += body
        out.append(self.wr(d, 'r', eav))
        if chain:
            out.append('ft_a = a; ft_b = k; ft_r = r;')
        out += fl
        out.append(cond_close)
        out.append('}')
        return [' '.join(x for x in out if x)]

    def i_shl(self, f, ins):
        return self._shift(f, ins, 'shl')

    i_sal = i_shl

    def i_shr(self, f, ins):
        return self._shift(f, ins, 'shr')

    def i_sar(self, f, ins):
        return self._shift(f, ins, 'sar')

    def _rot(self, f, ins, m):
        d = ins.ops[0]
        n = d.size
        bits = BITS[n]
        mask = hx(MASK[n])
        sb = bits - 1
        need = self.need(f, ins)
        imm = F.shift_count_is_imm(ins)
        cnt = (hx(ins.ops[1].imm & 31) if imm else '(c->cl & 31u)') if len(ins.ops) > 1 else '1u'
        out = ['{']
        eav = None
        if d.type == X.X86_OP_MEM:
            out.append(f'uint32_t ea = {self.ea(d)};')
            eav = 'ea'
        out.append(f'uint32_t a = {self.rd(d, eav)}, k = {cnt}, r = a;')
        out.append('if (k) {')
        if m in ('rol', 'ror'):
            out.append(f'uint32_t s = k % {bits};')
            if m == 'rol':
                out.append(f'r = s ? (((a << s) | (a >> ({bits} - s))) & {mask}) : a;')
                fl = ['c->cf = r & 1;', f'c->of = ((r >> {sb}) & 1) ^ (r & 1);']
            else:
                out.append(f'r = s ? (((a >> s) | (a << ({bits} - s))) & {mask}) : a;')
                fl = [f'c->cf = (r >> {sb}) & 1;', f'c->of = ((r >> {sb}) ^ (r >> {sb - 1})) & 1;']
        else:
            # Rotate through carry, one bit at a time (counts are tiny here).
            out.append(f'uint32_t s = k % {bits + 1}, cf = c->cf;')
            if m == 'rcl':
                out.append(f'while (s--) {{ uint32_t hi = (r >> {sb}) & 1; r = ((r << 1) | cf) & {mask}; cf = hi; }}')
                fl = ['c->cf = cf;', f'c->of = ((r >> {sb}) & 1) ^ cf;']
            else:
                out.append(f'uint32_t of0 = ((a >> {sb}) & 1) ^ cf;')
                out.append(f'while (s--) {{ uint32_t lo = r & 1; r = (r >> 1) | (cf << {sb}); cf = lo; }}')
                fl = ['c->cf = cf;', 'c->of = of0;']
        out.append(self.wr(d, 'r', eav))
        keep = []
        for s in fl:
            if s.startswith('c->cf') and need & F.CF or s.startswith('c->of') and need & F.OF:
                keep.append(s)
        out += keep
        out.append('}')
        out.append('}')
        return [' '.join(out)]

    def i_rol(self, f, ins):
        return self._rot(f, ins, 'rol')

    def i_ror(self, f, ins):
        return self._rot(f, ins, 'ror')

    def i_rcl(self, f, ins):
        return self._rot(f, ins, 'rcl')

    def i_rcr(self, f, ins):
        return self._rot(f, ins, 'rcr')

    # ------------------------------------------------------- mul and div
    def _mul_flags(self, need, overflow):
        out = []
        if need & (F.CF | F.OF):
            out.append(f'c->cf = c->of = ({overflow}) ? 1 : 0;')
        # SF/ZF/PF/AF are architecturally undefined; keep them deterministic.
        if need & F.ZF:
            out.append('c->zf = 0;')
        if need & F.SF:
            out.append('c->sf = 0;')
        if need & F.PF:
            out.append('c->pf = 0;')
        if need & F.AF:
            out.append('c->af = 0;')
        return out

    def i_mul(self, f, ins):
        s = ins.ops[0]
        n = s.size
        need = self.need(f, ins)
        src = self.rd(s)
        if n == 1:
            body = [f'uint32_t r = (uint32_t)c->al * ({src});', 'c->ax = (uint16_t)r;']
            ov = '(r >> 8) != 0'
        elif n == 2:
            body = [f'uint32_t r = (uint32_t)c->ax * ({src});', 'c->ax = (uint16_t)r;', 'c->dx = (uint16_t)(r >> 16);']
            ov = '(r >> 16) != 0'
        else:
            body = [f'uint64_t r = (uint64_t)c->eax * ({src});', 'c->eax = (uint32_t)r;', 'c->edx = (uint32_t)(r >> 32);']
            ov = '(r >> 32) != 0'
        return ['{ ' + ' '.join(body + self._mul_flags(need, ov)) + ' }']

    def i_imul(self, f, ins):
        need = self.need(f, ins)
        if len(ins.ops) == 1:
            s = ins.ops[0]
            n = s.size
            src = f'({SIGNED[n]})({self.rd(s)})'
            if n == 1:
                body = [f'int32_t r = (int32_t)(int8_t)c->al * {src};', 'c->ax = (uint16_t)r;']
                ov = 'r != (int8_t)r'
            elif n == 2:
                body = [f'int32_t r = (int32_t)(int16_t)c->ax * {src};', 'c->ax = (uint16_t)r;', 'c->dx = (uint16_t)((uint32_t)r >> 16);']
                ov = 'r != (int16_t)r'
            else:
                body = [f'int64_t r = (int64_t)(int32_t)c->eax * {src};', 'c->eax = (uint32_t)r;', 'c->edx = (uint32_t)((uint64_t)r >> 32);']
                ov = 'r != (int32_t)r'
            return ['{ ' + ' '.join(body + self._mul_flags(need, ov)) + ' }']
        if len(ins.ops) == 2:
            d, s = ins.ops
            a, b = self.rd(d), self.rd(s)
        else:
            d, s, k = ins.ops
            a, b = self.rd(s), hx(k.imm & MASK[d.size])
        n = d.size
        st = SIGNED[n]
        body = [f'int64_t r = (int64_t)({st})({a}) * (int64_t)({st})({b});', self.wr(d, '(uint32_t)r')]
        return ['{ ' + ' '.join(body + self._mul_flags(need, f'r != ({st})r')) + ' }']

    def i_div(self, f, ins):
        s = ins.ops[0]
        n = s.size
        need = self.need(f, ins)
        src = self.rd(s)
        a = hx(ins.addr)
        if n == 1:
            body = [f'uint32_t d = {src}, x = c->ax;', f'if (!d || x / d > 0xff) rt_trap(c, {a}, "divide error");',
                    'c->al = (uint8_t)(x / d); c->ah = (uint8_t)(x % d);']
        elif n == 2:
            body = [f'uint32_t d = {src}, x = ((uint32_t)c->dx << 16) | c->ax;',
                    f'if (!d || x / d > 0xffff) rt_trap(c, {a}, "divide error");',
                    'c->ax = (uint16_t)(x / d); c->dx = (uint16_t)(x % d);']
        else:
            body = [f'uint64_t d = {src}, x = ((uint64_t)c->edx << 32) | c->eax;',
                    f'if (!d || x / d > 0xffffffffull) rt_trap(c, {a}, "divide error");',
                    'c->eax = (uint32_t)(x / d); c->edx = (uint32_t)(x % d);']
        return ['{ ' + ' '.join(body + self._mul_flags(need, '0')) + ' }']

    def i_idiv(self, f, ins):
        s = ins.ops[0]
        n = s.size
        need = self.need(f, ins)
        src = self.rd(s)
        a = hx(ins.addr)
        if n == 1:
            body = [f'int32_t d = (int8_t)({src}), x = (int16_t)c->ax;',
                    f'if (!d || x / d > 127 || x / d < -128) rt_trap(c, {a}, "divide error");',
                    'c->al = (uint8_t)(x / d); c->ah = (uint8_t)(x % d);']
        elif n == 2:
            body = [f'int32_t d = (int16_t)({src}), x = (int32_t)(((uint32_t)c->dx << 16) | c->ax);',
                    f'if (!d || x / d > 32767 || x / d < -32768) rt_trap(c, {a}, "divide error");',
                    'c->ax = (uint16_t)(x / d); c->dx = (uint16_t)(x % d);']
        else:
            body = [f'int64_t d = (int32_t)({src}), x = (int64_t)(((uint64_t)c->edx << 32) | c->eax);',
                    f'if (!d || (x == INT64_MIN && d == -1) || x / d > INT32_MAX || x / d < INT32_MIN) rt_trap(c, {a}, "divide error");',
                    'c->eax = (uint32_t)(x / d); c->edx = (uint32_t)(x % d);']
        return ['{ ' + ' '.join(body + self._mul_flags(need, '0')) + ' }']

    def i_aam(self, f, ins):
        k = ins.ops[0].imm & 0xff if ins.ops else 10
        if k == 0:
            return [f'rt_trap(c, {hx(ins.addr)}, "divide error");']
        # AH = AL / k, AL = AL % k; SF/ZF/PF from AL, OF/AF/CF undefined (0).
        out = [f'{{ uint32_t t = c->al; c->ah = (uint8_t)(t / {k}u); c->al = (uint8_t)(t % {k}u);']
        out += self.flag_stmts('logic', 1, 'c->al', '0u', 'c->al', self.need(f, ins))
        out.append('}')
        return [' '.join(out)]

    # ------------------------------------------------------------ strings
    def _string(self, f, ins, op, size):
        rep = ''
        if ins.prefix:
            p = ins.prefix[0]
            if op in ('cmps', 'scas'):
                rep = {'rep': 'repe_', 'repe': 'repe_', 'repz': 'repe_', 'repne': 'repne_', 'repnz': 'repne_'}[p]
            else:
                rep = 'rep_'
        return [f'rt_{rep}{op}{size}(c);']

    def i_movsb(self, f, ins):
        return self._string(f, ins, 'movs', 1)

    def i_movsw(self, f, ins):
        return self._string(f, ins, 'movs', 2)

    def i_movsd(self, f, ins):
        if ins.ops and ins.ops[0].type == X.X86_OP_REG:
            raise LiftError('sse movsd')
        return self._string(f, ins, 'movs', 4)

    def i_stosb(self, f, ins):
        return self._string(f, ins, 'stos', 1)

    def i_stosw(self, f, ins):
        return self._string(f, ins, 'stos', 2)

    def i_stosd(self, f, ins):
        return self._string(f, ins, 'stos', 4)

    def i_lodsb(self, f, ins):
        return self._string(f, ins, 'lods', 1)

    def i_lodsw(self, f, ins):
        return self._string(f, ins, 'lods', 2)

    def i_lodsd(self, f, ins):
        return self._string(f, ins, 'lods', 4)

    def i_scasb(self, f, ins):
        return self._string(f, ins, 'scas', 1)

    def i_scasw(self, f, ins):
        return self._string(f, ins, 'scas', 2)

    def i_scasd(self, f, ins):
        return self._string(f, ins, 'scas', 4)

    def i_cmpsb(self, f, ins):
        return self._string(f, ins, 'cmps', 1)

    def i_cmpsw(self, f, ins):
        return self._string(f, ins, 'cmps', 2)

    def i_cmpsd(self, f, ins):
        return self._string(f, ins, 'cmps', 4)

    # ----------------------------------------------------------------- x87
    def i_fpu(self, f, ins):
        m = ins.mnem
        ops = ins.ops
        if m in ('wait', 'fwait', 'fnop'):
            return []
        mem = [o for o in ops if o.type == X.X86_OP_MEM]
        regs = [self.md.reg_name(o.reg) for o in ops if o.type == X.X86_OP_REG]

        def sti(name):
            if name == 'st(0)':
                return 0
            if name.startswith('st('):
                return int(name[3:-1])
            raise LiftError(f'fpu reg {name}')

        def memload(o, integer=False):
            ea = self.ea(o)
            if integer:
                return {2: f'(fpreg_t)(int16_t)R16({ea})', 4: f'(fpreg_t)(int32_t)R32({ea})',
                        8: f'(fpreg_t)(int64_t)R64({ea})'}[o.size]
            return {4: f'rt_f32(R32({ea}))', 8: f'rt_f64(R64({ea}))', 10: f'rt_f80_load({ea})'}[o.size]

        arith = {'fadd': 'a + b', 'fsub': 'a - b', 'fsubr': 'b - a', 'fmul': 'a * b',
                 'fdiv': 'a / b', 'fdivr': 'b / a'}
        base, integer = m, False
        if m.startswith('fi') and 'f' + m[2:] in arith:
            base, integer = 'f' + m[2:], True
        elif m.endswith('p') and m[:-1] in arith:
            base = m[:-1]
        if base in arith:
            expr = arith[base]
            pop = m.endswith('p')
            if mem:
                return [f'{{ fpreg_t a = ST(0), b = {memload(mem[0], integer)}; ST(0) = {expr}; }}']
            if len(regs) == 2:
                d, s = sti(regs[0]), sti(regs[1])
            elif len(regs) == 1:
                d, s = (sti(regs[0]), 0) if pop else (0, sti(regs[0]))
            else:
                d, s = 1, 0
            return [f'{{ fpreg_t a = ST({d}), b = ST({s}); ST({d}) = {expr}; }}' + (' FPOP();' if pop else '')]
        if m == 'fld':
            if mem:
                return [f'FPUSH({memload(mem[0])});']
            return [f'{{ fpreg_t v = ST({sti(regs[0])}); FPUSH(v); }}']
        if m == 'fild':
            return [f'FPUSH({memload(mem[0], True)});']
        if m in ('fst', 'fstp'):
            pop = ' FPOP();' if m == 'fstp' else ''
            if mem:
                o = mem[0]
                ea = self.ea(o)
                st = {4: f'W32({ea}, rt_f32_bits(ST(0)));', 8: f'W64({ea}, rt_f64_bits(ST(0)));',
                      10: f'rt_f80_store({ea}, ST(0));'}[o.size]
                return [st + pop]
            return [f'ST({sti(regs[0])}) = ST(0);' + pop]
        if m in ('fist', 'fistp'):
            o = mem[0]
            ea = self.ea(o)
            pop = ' FPOP();' if m == 'fistp' else ''
            return [{2: f'W16({ea}, (uint16_t)rt_fist(c, ST(0), 16));',
                     4: f'W32({ea}, (uint32_t)rt_fist(c, ST(0), 32));',
                     8: f'W64({ea}, (uint64_t)rt_fist(c, ST(0), 64));'}[o.size] + pop]
        if m == 'fxch':
            i = sti(regs[-1]) if regs else 1
            if i == 0 and len(regs) == 2:
                i = sti(regs[0])
            return [f'{{ fpreg_t t = ST(0); ST(0) = ST({i}); ST({i}) = t; }}']
        if m in ('fcom', 'fcomp', 'fucom', 'fucomp'):
            pop = ' FPOP();' if m.endswith('p') else ''
            if mem:
                return [f'rt_fcom(c, ST(0), {memload(mem[0])});' + pop]
            i = sti(regs[-1]) if regs else 1
            return [f'rt_fcom(c, ST(0), ST({i}));' + pop]
        if m in ('fcompp', 'fucompp'):
            return ['rt_fcom(c, ST(0), ST(1)); FPOP(); FPOP();']
        if m == 'ftst':
            return ['rt_fcom(c, ST(0), 0.0L);']
        if m in ('fnstsw', 'fstsw'):
            if mem:
                return [f'W16({self.ea(mem[0])}, rt_fpu_sw(c));']
            return ['c->ax = rt_fpu_sw(c);']
        if m in ('fnstcw', 'fstcw'):
            return [f'W16({self.ea(mem[0])}, c->fpu.cw);']
        if m == 'fldcw':
            # also loads precision and rounding control into the host x87
            return [f'rt_fpu_set_cw(c, R16({self.ea(mem[0])}));']
        if m in ('fninit', 'finit'):
            return ['rt_fpu_init(c);']
        if m in ('fnclex', 'fclex'):
            return ['c->fpu.sw &= 0x7f00u;']
        if m in ('fnsave', 'fsave'):
            return [f'rt_fpu_save(c, {self.ea(mem[0])});']
        if m == 'frstor':
            return [f'rt_fpu_restore(c, {self.ea(mem[0])});']
        unary = {'fchs': '-ST(0)', 'fabs': 'fabsl(ST(0))', 'fsqrt': 'sqrtl(ST(0))',
                 'frndint': 'rt_frndint(c, ST(0))'}
        if m in unary:
            return [f'ST(0) = {unary[m]};']
        consts = {'fldz': '0.0L', 'fld1': '1.0L'}
        if m in consts:
            return [f'FPUSH({consts[m]});']
        if m in ('fldl2t', 'fldl2e', 'fldpi', 'fldlg2', 'fldln2'):
            # rounded with the guest rounding control, like the x87 does
            return [f'FPUSH(rt_fpu_const(c, RT_{m.upper()}));']
        helpers = {'fprem': 'rt_fprem', 'fsin': 'rt_fsin', 'fcos': 'rt_fcos', 'fyl2x': 'rt_fyl2x'}
        if m in helpers:
            return [f'{helpers[m]}(c);']
        if m == 'fscale':
            return ['ST(0) = ldexpl(ST(0), (int)truncl(ST(1)));']
        if m == 'ffree':
            return []
        if m == 'fucomi' or m == 'fcomi':
            raise LiftError('fcomi')
        raise LiftError(f'x87 {m}')
