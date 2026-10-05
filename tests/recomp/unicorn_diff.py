#!/usr/bin/env python3
"""Differential test of the x86-32 -> C lifter against unicorn.

    python3 tests/recomp/unicorn_diff.py [--quick] [--seed N] [--only REGEX]

Method
  * WET.EXE is loaded and code is discovered exactly like tools/recomp
    (__main__.py): seeds = entry + Ghidra functions + config extra_entries,
    config blacklist, then flags.Liveness.
  * Every decoded instruction is grouped by (prefix+mnemonic, operand kinds and
    sizes). Groups with fewer than 60 members are tested completely; larger
    groups get one instance of every distinct encoding plus a deterministic
    sample up to 60 (--quick: 5 per group).
  * Each instruction is lifted by a Lifter subclass that materialises every
    flag the instruction defines and never fuses flag producers with
    consumers. Conditional branches keep their real lifted code; the branch
    target label records "taken". call/ret/jmp and traps are skipped.
  * Additionally every fused flag-producer/consumer chain the production
    lifter emits (cmp+jcc, test+setcc, ...) is lifted with the production
    Lifter and real liveness and compared on the flags that are live where
    the chain exits ("seq" cases).
  * All test functions are compiled into one executable (gcc -O1) with a
    driver that reads initial states, runs one test per state and writes the
    resulting CPU state plus every changed byte in the mapped regions.
    unicorn runs the original bytes from the same states.
  * Registers, branch outcome, architecturally defined flags, DF, memory and
    the x87 state (TOP, ST values, C0/C2/C3 where defined) are compared.

Output: a summary on stdout and build/recomp/unicorn_diff/{full,quick}/report.json.
Exit status 0 when every compared state matches.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures
import hashlib
import json
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import time
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

EXE = ROOT / 'original' / 'app' / 'WET.EXE'
RUNTIME = ROOT / 'src' / 'runtime'
OUT_DIR = ROOT / 'build' / 'recomp' / 'unicorn_diff'


def missing_requirements():
    missing = []
    for mod in ('capstone', 'unicorn', 'pefile'):
        try:
            __import__(mod)
        except ImportError:
            missing.append(f'python module {mod}')
    if not EXE.is_file():
        missing.append(str(EXE.relative_to(ROOT)))
    if shutil.which('gcc') is None:
        missing.append('gcc')
    return missing


# --------------------------------------------------------------- layout
LOW = (0x00000000, 0x10000)
SCRATCH = (0x10000000, 0x20000)
STACK = (0x20000000, 0x10000)
FS_BASE, FS_SIZE = 0x1001f000, 0x1000      # a TEB-like page at the end of scratch
GDT_BASE = 0x30000000                     # unicorn only
SEL_DATA, SEL_FS, SEL_SS = 0x2b, 0x53, 0x30
MASK32 = 0xffffffff

CF, PF, AF, ZF, SF, OF, DF = 1, 2, 4, 8, 16, 32, 64
FLAGS = (('cf', CF, 0), ('pf', PF, 2), ('af', AF, 4), ('zf', ZF, 6), ('sf', SF, 7),
         ('of', OF, 11), ('df', DF, 10))
STATUS = CF | PF | AF | ZF | SF | OF

REG32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI = range(8)
SUBREG = {}
for _i, _r in enumerate(REG32):
    SUBREG[_r] = (_i, 0, 32)
    SUBREG[_r[1:]] = (_i, 0, 16)
for _i, (_l, _h) in enumerate([('al', 'ah'), ('cl', 'ch'), ('dl', 'dh'), ('bl', 'bh')]):
    SUBREG[_l] = (_i, 0, 8)
    SUBREG[_h] = (_i, 8, 8)
SEGREGS = {'cs', 'ds', 'es', 'ss', 'fs', 'gs'}

STRING = {'movsb', 'movsw', 'movsd', 'stosb', 'stosw', 'stosd', 'lodsb', 'lodsw', 'lodsd',
          'scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd'}
SHIFTS = {'shl', 'sal', 'shr', 'sar'}
ROTATES = {'rol', 'ror', 'rcl', 'rcr'}
SHIFT_COUNTS = [0, 1, 7, 8, 15, 16, 31, 32, 33, 63, 255]

EDGES = {
    32: [0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff],
    16: [0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7ffe, 0x8001, 0xfffe],
    8: [0, 1, 0x7f, 0x80, 0xff, 0x40, 0x7e, 0x81, 0xfe, 0x0f, 0x10],
}
QUICK_EDGES = [0, 3, 8, 10]

# x87 comparison classes
# unicorn (QEMU 5) computes these in host doubles or inexactly: compared with
# a tolerance against unicorn and bit-exactly against the host x87
FPU_TRANSC = {'fsin', 'fcos', 'fyl2x', 'fprem', 'fptan', 'fpatan', 'f2xm1', 'fyl2xp1'}
HOST_REF = {'fsin': 1, 'fcos': 2, 'fyl2x': 3, 'fprem': 4}
# load-constant instructions: (truncated 64-bit mantissa, sign/exponent, round to
# nearest goes up); the x87 rounds them with RC, QEMU 5 always to nearest
FPU_CONST = {'fldl2t': (0xd49a784bcd1b8afe, 0x4000, 0), 'fldl2e': (0xb8aa3b295c17f0bb, 0x3fff, 1),
             'fldpi': (0xc90fdaa22168c234, 0x4000, 1), 'fldlg2': (0x9a209a84fbcff798, 0x3ffd, 1),
             'fldln2': (0xb17217f7d1cf79ab, 0x3ffe, 1)}
FPU_COMPARE = {'fcom', 'fcomp', 'fcompp', 'fucom', 'fucomp', 'fucompp', 'ftst', 'ficom', 'ficomp'}
FPU_RC = {'fist', 'fistp', 'frndint'}
FPU_INIT = {'fninit', 'finit', 'fnsave', 'fsave'}


def s32(v):
    v &= MASK32
    return v - (1 << 32) if v & 0x80000000 else v


# ------------------------------------------------------- 80-bit helpers
def f64_to_f80(x):
    """Exact (mantissa, sign_exponent) of a double."""
    b = struct.unpack('<Q', struct.pack('<d', x))[0]
    sign, e, frac = b >> 63, (b >> 52) & 0x7ff, b & ((1 << 52) - 1)
    if e == 0x7ff:
        return (1 << 63) | (frac << 11), (sign << 15) | 0x7fff
    if e == 0:
        if frac == 0:
            return 0, sign << 15
        p = frac.bit_length() - 1
        return frac << (63 - p), (sign << 15) | (p - 1074 + 16383)
    return (1 << 63) | (frac << 11), (sign << 15) | (e - 1023 + 16383)


def f80_to_f64(mant, se):
    """Correctly rounded double of an 80-bit value."""
    neg = se >> 15
    e = se & 0x7fff
    if e == 0x7fff:
        if (mant << 1) & ((1 << 64) - 1) == 0:
            return -math.inf if neg else math.inf
        return math.nan
    if mant == 0:
        return -0.0 if neg else 0.0
    ex = (e if e else 1) - 16383 - 63
    try:
        v = float(mant << ex) if ex >= 0 else mant / (1 << -ex)
    except OverflowError:
        v = math.inf
    return -v if neg else v


def f80_is_double(mant, se):
    """True when the 80-bit value is exactly a double (or a NaN/inf/zero)."""
    e = se & 0x7fff
    if e == 0x7fff or mant == 0:
        return True
    return f64_to_f80(f80_to_f64(mant, se)) == (mant, se)


def f80_isnan(t):
    return (t[1] & 0x7fff) == 0x7fff and (t[0] << 1) & ((1 << 64) - 1) != 0


def f80_fraction(mant, se):
    e = se & 0x7fff
    v = Fraction(mant) * Fraction(2) ** ((e if e else 1) - 16383 - 63)
    return -v if se >> 15 else v


def fraction_to_f80(v, neg_zero=False):
    """Exact 80-bit encoding of a rational that has one (else None)."""
    if v == 0:
        return 0, 0x8000 if neg_zero else 0
    sign = 0x8000 if v < 0 else 0
    v = abs(v)
    k = v.numerator.bit_length() - v.denominator.bit_length()
    if Fraction(2) ** k > v:
        k -= 1
    e = k + 16383                        # biased exponent of 1.f * 2^k
    shift = 63 - k if e >= 1 else 16382 + 63
    m = v * Fraction(2) ** shift
    if m.denominator != 1 or m.numerator >> 64:
        return None
    return int(m), sign | (e if e >= 1 else 0)


def f80_bytes(mant, se):
    return struct.pack('<QH', mant, se)


def fmt80(t):
    v = f80_to_f64(*t)
    return f'{v!r}' if f80_is_double(*t) else f'{v!r}~[{t[1]:04x}:{t[0]:016x}]'


def dbits(x):
    return struct.unpack('<Q', struct.pack('<d', x))[0]


# ------------------------------------------------------------ the image
class World:
    """The discovered program plus the memory regions both sides start from."""

    def __init__(self, seed):
        from tools.recomp import __main__ as M, cfg, flags, pe
        self.img = pe.load(str(EXE))
        extra, blacklist = M.load_config()
        seeds = {self.img.entry} | M.seeds_from_ghidra(M.FUNCTIONS_TSV) | extra
        self.prog = cfg.Program(self.img)
        self.prog.discover(seeds, blacklist=blacklist)
        self.live = flags.Liveness(self.prog).solve()
        size = (self.img.size + 0xfff) & ~0xfff
        rng = random.Random(f'memory:{seed}')
        image = bytearray(size)
        image[:len(self.img.memory)] = self.img.memory
        self.regions = [
            (LOW[0], bytearray(rng.randbytes(LOW[1]))),
            (self.img.base, image),
            (SCRATCH[0], bytearray(rng.randbytes(SCRATCH[1]))),
            (STACK[0], bytearray(rng.randbytes(STACK[1]))),
        ]

    def region_of(self, addr, size=1):
        for base, data in self.regions:
            if base <= addr and addr + size <= base + len(data):
                return base, data
        return None

    def pristine(self, addr, size):
        r = self.region_of(addr, size)
        if r is None:
            return None
        base, data = r
        return bytes(data[addr - base:addr - base + size])

    def write_regions(self, path):
        out = bytearray(struct.pack('<I', len(self.regions)))
        for base, data in self.regions:
            out += struct.pack('<II', base, len(data)) + data
        path.write_bytes(out)


# --------------------------------------------------------- the lifters
def make_test_lifter():
    from tools.recomp import flags as F, lift

    class TestLifter(lift.Lifter):
        """Materialises every flag an instruction defines; never fuses."""

        def need(self, f, ins):
            _, d, cd = F.use_def(ins)
            return d | cd

        def fusion_chain(self, f, setter):
            return [], 0

        def body(self, f, ins):
            self.f = f
            self.conts = set()
            self.preds = {}
            self.fused = {}
            return self.insn(f, ins)

    return TestLifter


class State:
    __slots__ = ('regs', 'flags', 'fpu', 'patches', 'note')

    def __init__(self):
        self.regs = [0] * 8
        self.flags = 0
        self.fpu = None
        self.patches = []
        self.note = ''

    def describe(self):
        regs = ' '.join(f'{n}={v:08x}' for n, v in zip(REG32, self.regs))
        fl = ''.join(n.upper() if self.flags & bit else n for n, bit, _ in FLAGS)
        out = f'{regs} flags={fl}'
        if self.fpu:
            fp = self.fpu
            sts = ' '.join(f'ST{i}={fmt80(fp["st"][(fp["top"] + i) & 7])}' for i in range(8))
            out += (f'\n      fpu top={fp["top"]} cw={fp["cw"]:04x} sw={fp["sw"]:02x} '
                    f'c0..3={"".join(map(str, fp["c"]))} {sts}')
        for a, b in self.patches:
            out += f'\n      mem[{a:08x}] = {b.hex()}'
        if self.note:
            out += f'\n      note: {self.note}'
        return out


class Result:
    __slots__ = ('trapped', 'exit', 'regs', 'flags', 'fpu', 'mem', 'invalid')

    def __init__(self):
        self.trapped = False
        self.exit = None
        self.regs = None
        self.flags = {}
        self.fpu = None
        self.mem = {}
        self.invalid = None

    def describe(self, case):
        if self.invalid:
            return f'invalid: {self.invalid}'
        regs = ' '.join(f'{n}={v:08x}' for n, v in zip(REG32, self.regs))
        fl = ' '.join(f'{n}={self.flags[n]}' for n, _, _ in FLAGS)
        out = f'trapped={int(self.trapped)} exit={self.exit:08x} {regs}\n      {fl}'
        if self.fpu:
            fp = self.fpu
            sts = ' '.join(f'ST{i}={fmt80(fp["st"][(fp["top"] + i) & 7])}' for i in range(8))
            out += (f'\n      fpu top={fp["top"]} cw={fp["cw"]:04x} '
                    f'c0..3={"".join(map(str, fp["c"]))} {sts}')
        if self.mem:
            items = sorted(self.mem.items())
            shown = ' '.join(f'{a:08x}:{v:02x}' for a, v in items[:24])
            out += f'\n      mem({len(items)}) {shown}' + (' ...' if len(items) > 24 else '')
        return out


# ---------------------------------------------------------------- cases
class Case:
    def __init__(self, kind, func, seq, group, sig=None):
        self.kind = kind                  # 'insn', 'seq' (fused chain) or 'fseq' (x87 status)
        self.func = func
        self.seq = seq                    # instructions executed in order
        self.ins = seq[0]                 # the instruction that gets operands
        self.group = group
        self.sig = sig
        self.m = self.ins.mnem
        self.idx = None
        self.body = None                  # C source of the test function
        self.error = None                 # LiftError text
        self.branch = any(i.flow == 'jcc' for i in seq)
        self.fpu = self.m.startswith('f') or self.m == 'wait'
        self.exit_live = None             # seq: {exit address: live flag mask}

    @property
    def text(self):
        return '; '.join(f'{i.cs.mnemonic} {i.cs.op_str}'.strip() for i in self.seq)

    @property
    def addr(self):
        return self.ins.addr


def reg_name(ins, r):
    return ins.cs.reg_name(r)


def operand_kind(o):
    from capstone import x86 as X
    if o.type == X.X86_OP_REG:
        return f'r{o.size * 8}'
    if o.type == X.X86_OP_IMM:
        return f'i{o.size * 8}'
    return f'm{o.size * 8}'


def raw_rep(ins):
    """'rep' / 'repne' from the prefix bytes themselves (capstone drops F2 on
    some string forms, e.g. f2 a5), or None."""
    rep = None
    for byte in bytes(ins.cs.bytes):
        if byte == 0xf3:
            rep = 'rep'
        elif byte == 0xf2:
            rep = 'repne'
        elif byte not in (0xf0, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0x66, 0x67):
            break
    return rep


def group_key(ins):
    prefix = [raw_rep(ins)] if ins.mnem in STRING and raw_rep(ins) else \
        [p for p in ins.prefix if p not in ('rep', 'repe', 'repne', 'repz', 'repnz')]
    return (' '.join(prefix + [ins.mnem]) + ' ' + ','.join(operand_kind(o) for o in ins.ops)).strip()


def enc_sig(ins):
    """Encoding form: opcode bytes, ModRM/SIB shape, operand classes."""
    from capstone import x86 as X
    cs = ins.cs
    enc = cs.encoding
    b = bytes(cs.bytes)
    end = len(b)
    for off in (enc.modrm_offset, enc.disp_offset, enc.imm_offset):
        if off:
            end = min(end, off)
    mod = rm_sib = None
    if enc.modrm_offset:
        mod = cs.modrm >> 6
        rm_sib = (cs.modrm & 7) == 4 and mod != 3
    classes = []
    for o in ins.ops:
        if o.type == X.X86_OP_REG:
            n = reg_name(ins, o.reg)
            classes.append('h8' if n in ('ah', 'bh', 'ch', 'dh') else
                           'sp' if n in ('esp', 'sp') else 'bp' if n in ('ebp', 'bp') else
                           'seg' if n in SEGREGS else 'st' if n.startswith('st') else 'r')
        elif o.type == X.X86_OP_MEM:
            base = reg_name(ins, o.mem.base) if o.mem.base else '-'
            classes.append(('m', base if base in ('esp', 'ebp', '-') else 'b',
                            bool(o.mem.index), o.mem.scale, o.mem.segment == X.X86_REG_FS))
        else:
            classes.append('i')
    regs = [o.reg for o in ins.ops if o.type == X.X86_OP_REG]
    same = len(regs) == 2 and regs[0] == regs[1]
    return (b[:end].hex(), mod, rm_sib, tuple(classes), same, enc.imm_size, enc.disp_size)


def skip_reason(ins):
    from capstone import x86 as X
    if ins.flow in ('call', 'calli'):
        return 'call'
    if ins.flow == 'ret':
        return 'ret'
    if ins.flow in ('jmp', 'jmpi'):
        return 'jmp'
    if ins.flow == 'trap':
        return 'trap'
    if ins.mnem == 'mov' and ins.ops[0].type == X.X86_OP_REG and reg_name(ins, ins.ops[0].reg) == 'ss':
        return 'loads SS (needs CPL 3 in unicorn; the lifter ignores segment loads)'
    return None


def select_cases(world, quick, seed, only):
    prog = world.prog
    owner = {}
    for e in sorted(prog.functions):
        for a in prog.functions[e].insns:
            owner.setdefault(a, prog.functions[e])
    groups = collections.defaultdict(list)
    skipped = collections.Counter()
    for a in sorted(owner):
        ins = owner[a].insns[a]
        key = group_key(ins)
        reason = skip_reason(ins)
        if reason:
            skipped[f'{key}: {reason}'] += 1
            continue
        groups[key].append(ins)
    limit = 5 if quick else 60
    cases, sizes = [], {}
    for key in sorted(groups):
        members = groups[key]
        sizes[key] = len(members)
        if only and not re.search(only, key):
            continue
        if len(members) < limit:
            chosen = list(members)
        else:
            chosen, seen = [], set()
            for ins in members:
                s = enc_sig(ins)
                if s not in seen:
                    seen.add(s)
                    chosen.append(ins)
            rng = random.Random(f'{seed}:{key}')
            if quick:
                chosen = rng.sample(chosen, min(len(chosen), limit))
            have = {i.addr for i in chosen}
            pool = [i for i in members if i.addr not in have]
            chosen += rng.sample(pool, max(0, min(len(pool), limit - len(chosen))))
        for ins in sorted(chosen, key=lambda i: i.addr):
            cases.append(Case('insn', owner[ins.addr], [ins], key, enc_sig(ins)))
    return cases, sizes, skipped


def select_sequences(world, quick, seed, only):
    """Fused flag producer + consumer chains as the production lifter emits them."""
    from tools.recomp import flags as F, lift
    prog = world.prog
    host = {slot: imp.host_symbol for slot, imp in world.img.imports.items()}
    lifter = lift.Lifter(prog, world.live, host)
    chains = collections.defaultdict(list)
    seen = set()
    for e in sorted(prog.functions):
        f = prog.functions[e]
        lifter.function(f)
        by_setter = collections.defaultdict(list)
        for r, s in lifter.fused.items():
            by_setter[s.addr].append(r)
        for s, readers in by_setter.items():
            if (e, s) in seen:
                continue
            seen.add((e, s))
            setter = f.insns[s]
            seq = [setter] + [f.insns[r] for r in sorted(readers)]
            if any(i.flow == 'jcc' and i.targets[0] == i.next for i in seq):
                continue
            key = 'seq ' + group_key(setter) + ' -> ' + ','.join(F.cc_of(i.mnem) + ('' if i.flow == 'jcc' else '(set)') for i in seq[1:])
            chains[key].append((f, seq))
    # x87 status sequences: compare (or fnstsw) ... sahf/test ... jcc
    starters = FPU_COMPARE | {'fprem', 'fnstsw', 'fstsw', 'fldcw'}
    seen_start = set()
    for e in sorted(prog.functions):
        f = prog.functions[e]
        for a in sorted(f.insns):
            first = f.insns[a]
            if first.mnem not in starters or a in seen_start:
                continue
            seen_start.add(a)
            seq, cur = [first], first
            while len(seq) < 10:
                nxt = f.insns.get(cur.next)
                if nxt is None or nxt.flow not in ('seq', 'jcc') or skip_reason(nxt):
                    break
                seq.append(nxt)
                cur = nxt
                if nxt.flow == 'jcc':
                    break
            if first.mnem == 'fldcw':
                # the new control word must reach the host x87 for what follows
                if len(seq) < 2 or (seq[-1].flow == 'jcc' and seq[-1].targets[0] == seq[-1].next):
                    continue
            elif seq[-1].flow != 'jcc' or seq[-1].targets[0] == seq[-1].next:
                continue
            key = 'fseq ' + ' ; '.join(i.mnem for i in seq)
            chains[key].append((f, seq))
    limit = 2 if quick else None
    cases, sizes = [], {}
    for key in sorted(chains):
        members = chains[key]
        sizes[key] = len(members)
        if only and not re.search(only, key):
            continue
        rng = random.Random(f'{seed}:{key}')
        chosen = members if limit is None or len(members) <= limit else rng.sample(members, limit)
        for f, seq in sorted(chosen, key=lambda x: x[1][0].addr):
            cases.append(Case(key.split()[0], f, seq, key))
    return cases, sizes, lifter


def live_in(world, f, addr):
    from tools.recomp import flags as F
    ins = f.insns.get(addr)
    if ins is None:
        return None
    use, d, _ = world.live.local_use_def(f, ins)
    return use | (world.live.live_out.get((f.entry, addr), F.ALL) & ~d)


# --------------------------------------------------------- C generation
LABEL_RE = re.compile(r'goto L_([0-9a-f]{8});')


def c_function(name, case, bodies):
    lines = [f'void {name}(Cpu *c, uint32_t *exitp)', '{',
             '    uint32_t ra; uint32_t ft_a = 0, ft_b = 0, ft_r = 0;',
             '    (void)ra; (void)ft_a; (void)ft_b; (void)ft_r;']
    labels = []
    for ins, body in zip(case.seq, bodies):
        text = f'{ins.cs.mnemonic} {ins.cs.op_str}'.strip().replace('*/', '* /')
        lines.append(f'    /* {ins.addr:08x}: {text} */')
        lines += ['    ' + b for b in body]
        for b in body:
            labels += LABEL_RE.findall(b)
    lines.append(f'    *exitp = 0x{case.seq[-1].next:08x}u; return;')
    for t in dict.fromkeys(labels):
        lines.append(f'L_{t}: *exitp = 0x{t}u; return;')
    lines.append('}')
    return '\n'.join(lines)


def build_bodies(world, cases, seq_lifter):
    from tools.recomp import lift
    tl = make_test_lifter()(world.prog, world.live, {})
    for c in cases:
        try:
            if c.kind == 'insn':
                bodies = [tl.body(c.func, c.ins)]
            else:
                L = seq_lifter
                f = c.func
                L.f = f
                L.conts = L.continuations(f)
                L.preds = L.pred_count(f)
                L.fused = {}
                bodies = [L.insn(f, i) for i in c.seq]
                if c.kind == 'seq' and any(L.fused.get(i.addr) is not c.seq[0] for i in c.seq[1:]):
                    raise lift.LiftError('chain not fused as expected')
            for b in bodies:
                if any('return' in x for x in b):
                    raise lift.LiftError('body returns (control transfer)')
            c.bodies = bodies
        except lift.LiftError as e:
            c.error = str(e)


DRIVER_C = r'''
#define _GNU_SOURCE
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "rt_cpu.h"

typedef void (*TestFn)(Cpu *c, uint32_t *exitp);
extern const TestFn g_tests[];
extern const uint32_t g_test_count;

#if !RT_HOST_X87
#error "the harness compares 80-bit registers and needs an x86 host with an x87 long double"
#endif

uint8_t *g_mem;
volatile int rt_gil_waiters;          /* RT_POLL() in backward jumps: never set here */
void rt_gil_yield(void) {}
static jmp_buf trap_env;
static Cpu cpu;
static uint32_t exit_addr;

void rt_trap(Cpu *c, uint32_t addr, const char *what) { (void)c; (void)addr; (void)what; longjmp(trap_env, 1); }
uint32_t rt_unimplemented(Cpu *c, const char *name) { (void)c; fprintf(stderr, "rt_unimplemented %s\n", name); exit(3); }
uint32_t rt_call_indirect(Cpu *c, uint32_t t) { (void)c; fprintf(stderr, "rt_call_indirect %08x\n", t); exit(3); }
uint32_t rt_jump_indirect(Cpu *c, uint32_t t) { (void)c; fprintf(stderr, "rt_jump_indirect %08x\n", t); exit(3); }
uint32_t rt_get_eflags(Cpu *c)
{
    return c->cf | c->pf << 2 | c->af << 4 | c->zf << 6 | c->sf << 7 | c->df << 10 | c->of << 11 | 0x202u;
}
void rt_set_eflags(Cpu *c, uint32_t v)
{
    c->cf = v & 1; c->pf = (v >> 2) & 1; c->af = (v >> 4) & 1; c->zf = (v >> 6) & 1;
    c->sf = (v >> 7) & 1; c->df = (v >> 10) & 1; c->of = (v >> 11) & 1;
}

typedef struct { uint32_t addr, size; uint8_t *pristine; } Region;
static Region regions[16];
static int nregions;

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len + 1);
    if (fread(b, 1, *len, f) != *len) { perror(path); exit(2); }
    fclose(f);
    return b;
}

static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t get16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

/* The host x87 itself, as a reference for what unicorn computes inexactly. */
static void host_ref(uint32_t op, Cpu *c, uint8_t out[10], uint16_t *swp)
{
    long double x = ST(0), y = ST(1), r = 0;
    uint16_t sw = 0;
    switch (op) {
    case 1: __asm__("fsin\n\tfnstsw %1" : "+t"(x), "=a"(sw)); r = x; break;
    case 2: __asm__("fcos\n\tfnstsw %1" : "+t"(x), "=a"(sw)); r = x; break;
    case 3: __asm__("fyl2x\n\tfnstsw %1" : "=t"(r), "=a"(sw) : "0"(x), "u"(y) : "st(1)"); break;
    case 4: __asm__("fprem\n\tfnstsw %1" : "+t"(x), "=a"(sw) : "u"(y)); r = x; break;
    }
    memcpy(out, &r, 10);
    *swp = sw;
}

static uint8_t outbuf[1 << 20];
static size_t outlen;
static void put(const void *p, size_t n) { memcpy(outbuf + outlen, p, n); outlen += n; }
static void put32(uint32_t v) { put(&v, 4); }

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s regions vectors results\n", argv[0]); return 2; }
    g_mem = mmap(NULL, (1ull << 32) + 0x10000, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (g_mem == MAP_FAILED) { perror("mmap"); return 2; }
    size_t n, pos = 4;
    uint8_t *r = slurp(argv[1], &n);
    nregions = (int)get32(r);
    for (int i = 0; i < nregions; i++) {
        Region *g = &regions[i];
        g->addr = get32(r + pos); g->size = get32(r + pos + 4); pos += 8;
        memcpy(g_mem + g->addr, r + pos, g->size);
        g->pristine = malloc(g->size);
        memcpy(g->pristine, r + pos, g->size);
        pos += g->size;
    }
    uint8_t *v = slurp(argv[2], &n);
    FILE *out = fopen(argv[3], "wb");
    if (!out) { perror(argv[3]); return 2; }
    pos = 0;
    while (pos < n) {
        const uint8_t *p = v + pos;
        uint32_t test = get32(p);
        memset(&cpu, 0, sizeof cpu);
        cpu.eax = get32(p + 4); cpu.ecx = get32(p + 8); cpu.edx = get32(p + 12); cpu.ebx = get32(p + 16);
        cpu.esp = get32(p + 20); cpu.ebp = get32(p + 24); cpu.esi = get32(p + 28); cpu.edi = get32(p + 32);
        cpu.cf = p[36]; cpu.pf = p[37]; cpu.af = p[38]; cpu.zf = p[39]; cpu.sf = p[40]; cpu.of = p[41]; cpu.df = p[42];
        cpu.fs_base = get32(p + 44);
        cpu.fpu.top = get32(p + 48); cpu.fpu.cw = get16(p + 52); cpu.fpu.sw = get16(p + 54);
        cpu.fpu.c0 = p[56]; cpu.fpu.c1 = p[57]; cpu.fpu.c2 = p[58]; cpu.fpu.c3 = p[59];
        for (int i = 0; i < 8; i++)
            memcpy(&cpu.fpu.st[i], p + 60 + 10 * i, 10);
        uint32_t refop = get32(p + 140);
        uint32_t npatch = get32(p + 144);
        pos += 148;
        rt_fpu_sync_host(&cpu);
        uint8_t ref[10] = {0};
        uint16_t ref_sw = 0;
        if (refop)
            host_ref(refop, &cpu, ref, &ref_sw);
        for (uint32_t i = 0; i < npatch; i++) {
            uint32_t a = get32(v + pos), len = get32(v + pos + 4);
            memcpy(g_mem + a, v + pos + 8, len);
            pos += 8 + len;
        }
        if (test >= g_test_count) { fprintf(stderr, "bad test %u\n", test); return 2; }
        exit_addr = 0xffffffffu;
        uint32_t trapped = 0;
        if (setjmp(trap_env) == 0)
            g_tests[test](&cpu, &exit_addr);
        else
            trapped = 1;
        outlen = 0;
        put32(test); put32(trapped); put32(exit_addr);
        put32(cpu.eax); put32(cpu.ecx); put32(cpu.edx); put32(cpu.ebx);
        put32(cpu.esp); put32(cpu.ebp); put32(cpu.esi); put32(cpu.edi);
        put32(cpu.cf); put32(cpu.pf); put32(cpu.af); put32(cpu.zf); put32(cpu.sf); put32(cpu.of); put32(cpu.df);
        put32(cpu.fpu.top); put(&cpu.fpu.cw, 2); put(&cpu.fpu.sw, 2);
        put(&cpu.fpu.c0, 1); put(&cpu.fpu.c1, 1); put(&cpu.fpu.c2, 1); put(&cpu.fpu.c3, 1);
        for (int i = 0; i < 8; i++)
            put(&cpu.fpu.st[i], 10);
        put(&ref_sw, 2);
        put(ref, 10);
        size_t ndiff_at = outlen;
        uint32_t ndiff = 0;
        put32(0);
        for (int i = 0; i < nregions; i++) {
            Region *g = &regions[i];
            uint8_t *cur = g_mem + g->addr;
            for (uint32_t off = 0; off < g->size; off += 64) {
                if (memcmp(cur + off, g->pristine + off, 64) == 0)
                    continue;
                for (uint32_t b = off; b < off + 64; b++) {
                    if (cur[b] != g->pristine[b]) {
                        if (ndiff < 60000) { put32(g->addr + b); put32(cur[b]); ndiff++; }
                        cur[b] = g->pristine[b];
                    }
                }
            }
        }
        memcpy(outbuf + ndiff_at, &ndiff, 4);
        fwrite(outbuf, 1, outlen, out);
    }
    fclose(out);
    return 0;
}
'''

IN_FMT = '<I8I8BIIHH4B80sII'
OUT_FMT = '<III8I7IIHH4B80sH10sI'
assert struct.calcsize(IN_FMT) == 148 and struct.calcsize(OUT_FMT) == 180


def build_binary(cases, workdir, jobs):
    """Compile all test functions plus the driver; returns the executable path."""
    workdir.mkdir(parents=True, exist_ok=True)
    runnable = [c for c in cases if c.error is None]
    for i, c in enumerate(runnable):
        c.idx = i
    chunks = [runnable[i:i + 300] for i in range(0, len(runnable), 300)]
    sources = []
    for n, chunk in enumerate(chunks):
        text = ['#include "rt_cpu.h"', '#include <math.h>', '']
        for c in chunk:
            text.append(c_function(f't_{c.idx}', c, c.bodies))
            text.append('')
        sources.append((f'tests_{n:03d}.c', '\n'.join(text)))
    table = ['#include "rt_cpu.h"', 'typedef void (*TestFn)(Cpu *c, uint32_t *exitp);']
    table += [f'void t_{c.idx}(Cpu *c, uint32_t *exitp);' for c in runnable]
    table.append('const TestFn g_tests[] = {' + ', '.join(f't_{c.idx}' for c in runnable) + '};')
    table.append(f'const uint32_t g_test_count = {len(runnable)};')
    sources.append(('table.c', '\n'.join(table) + '\n'))
    sources.append(('driver.c', DRIVER_C))
    sources.append(('rt_fpu.c', (RUNTIME / 'rt_fpu.c').read_text()))
    header = (RUNTIME / 'rt_cpu.h').read_text()
    cflags = ['-std=gnu11', '-O1', '-w', f'-I{RUNTIME}']

    def compile_one(name_text):
        name, text = name_text
        src = workdir / name
        key = hashlib.sha256((text + header + ' '.join(cflags)).encode()).hexdigest()[:16]
        obj = workdir / f'{name[:-2]}.{key}.o'
        if not obj.exists():
            src.write_text(text)
            r = subprocess.run(['gcc', *cflags, '-c', str(src), '-o', str(obj)],
                               capture_output=True, text=True)
            if r.returncode:
                raise RuntimeError(f'gcc failed for {src}:\n{r.stderr[:4000]}')
        return obj

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        objs = list(ex.map(compile_one, sources))
    exe = workdir / 'difftest'
    r = subprocess.run(['gcc', *map(str, objs), '-o', str(exe), '-lm'], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f'link failed:\n{r.stderr[:4000]}')
    for old in workdir.glob('*.o'):
        if old not in objs:
            old.unlink()
    return exe


def pack_state(case, st):
    fpu = st.fpu or {'top': 0, 'cw': 0x037f, 'sw': 0, 'c': [0, 0, 0, 0], 'st': [(0, 0)] * 8}
    fl = [1 if st.flags & bit else 0 for _, bit, _ in FLAGS] + [0]
    regs80 = b''.join(f80_bytes(*t) for t in fpu['st'])
    refop = HOST_REF.get(case.m, 0) if case.kind == 'insn' and st.fpu else 0
    out = bytearray(struct.pack(IN_FMT, case.idx, *st.regs, *fl, FS_BASE, fpu['top'], fpu['cw'],
                                fpu['sw'], *fpu['c'], regs80, refop, len(st.patches)))
    for a, b in st.patches:
        out += struct.pack('<II', a, len(b)) + b
    return out


def run_c(exe, workdir, world, jobs_list):
    vec = bytearray()
    for case, st in jobs_list:
        vec += pack_state(case, st)
    regions = workdir / 'regions.bin'
    world.write_regions(regions)
    vpath, rpath = workdir / 'vectors.bin', workdir / 'results.bin'
    vpath.write_bytes(vec)
    r = subprocess.run([str(exe), str(regions), str(vpath), str(rpath)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f'test binary failed ({r.returncode}): {r.stderr[:2000]}')
    data = rpath.read_bytes()
    pos, out = 0, []
    hsize = struct.calcsize(OUT_FMT)
    for case, st in jobs_list:
        v = struct.unpack_from(OUT_FMT, data, pos)
        pos += hsize
        res = Result()
        if v[0] != case.idx:
            raise RuntimeError('result stream out of sync')
        res.trapped, res.exit = bool(v[1]), v[2]
        res.regs = list(v[3:11])
        res.flags = {n: v[11 + i] for i, (n, _, _) in enumerate(FLAGS)}
        top, cw, sw = v[18], v[19], v[20]
        st80 = [struct.unpack_from('<QH', v[25], 10 * i) for i in range(8)]
        res.fpu = {'top': top, 'cw': cw, 'sw': sw, 'c': list(v[21:25]), 'st': st80,
                   'ref_sw': v[26], 'ref': struct.unpack('<QH', v[27])}
        ndiff = v[28]
        for i in range(ndiff):
            a, b = struct.unpack_from('<II', data, pos + 8 * i)
            res.mem[a] = b
        pos += 8 * ndiff
        out.append(res)
    return out


# -------------------------------------------------------------- unicorn
class Oracle:
    def __init__(self, world):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_WRITE, UC_HOOK_INTR
        from unicorn import x86_const as XC
        self.XC = XC
        self.world = world
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        for base, data in world.regions:
            uc.mem_map(base, len(data))
            uc.mem_write(base, bytes(data))
        uc.mem_map(GDT_BASE, 0x1000)

        def desc(base, limit, access, fl):
            d = (limit & 0xffff) | ((base & 0xffffff) << 16) | (access << 40)
            d |= ((limit >> 16) & 0xf) << 48 | (fl << 52) | (((base >> 24) & 0xff) << 56)
            return struct.pack('<Q', d)
        uc.mem_write(GDT_BASE + (SEL_DATA >> 3) * 8, desc(0, 0xfffff, 0xf2, 0xc))   # data, DPL 3
        uc.mem_write(GDT_BASE + (SEL_SS >> 3) * 8, desc(0, 0xfffff, 0x92, 0xc))     # stack, DPL 0
        uc.mem_write(GDT_BASE + (SEL_FS >> 3) * 8, desc(FS_BASE, FS_SIZE - 1, 0xf2, 0x4))
        uc.reg_write(XC.UC_X86_REG_GDTR, (0, GDT_BASE, 0xfff, 0))
        self.reset_segments()
        # unicorn keeps the exception state of a #DE (the next one becomes a
        # double fault); a context saved here is restored after every trap.
        self.clean = uc.context_save()
        self.gpr = [getattr(XC, f'UC_X86_REG_{r.upper()}') for r in REG32]
        self.fp = [getattr(XC, f'UC_X86_REG_FP{i}') for i in range(8)]
        self.writes = []
        self.intr = None
        uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        uc.hook_add(UC_HOOK_INTR, self._on_intr)

    def reset_segments(self):
        XC = self.XC
        self.uc.reg_write(XC.UC_X86_REG_SS, SEL_SS)
        for r in ('DS', 'ES', 'GS'):
            self.uc.reg_write(getattr(XC, f'UC_X86_REG_{r}'), SEL_DATA)
        self.uc.reg_write(XC.UC_X86_REG_FS, SEL_FS)

    def _on_write(self, uc, access, addr, size, value, user):
        self.writes.append((addr, size))

    def _on_intr(self, uc, intno, user):
        self.intr = intno
        uc.emu_stop()

    def run(self, case, st):
        from unicorn import UcError
        uc, XC = self.uc, self.XC
        res = Result()
        for r, v in zip(self.gpr, st.regs):
            uc.reg_write(r, v)
        ef = 0x202
        for _, bit, sh in FLAGS:
            if st.flags & bit:
                ef |= 1 << sh
        uc.reg_write(XC.UC_X86_REG_EFLAGS, ef)
        if st.fpu:
            fp = st.fpu
            uc.reg_write(XC.UC_X86_REG_FPCW, fp['cw'])
            c = fp['c']
            uc.reg_write(XC.UC_X86_REG_FPSW, fp['sw'] | c[0] << 8 | c[1] << 9 | c[2] << 10 |
                         fp['top'] << 11 | c[3] << 14)
            uc.reg_write(XC.UC_X86_REG_FPTAG, 0)
            for i in range(8):
                uc.reg_write(self.fp[i], fp['st'][i])
        dirty = []
        for a, b in st.patches:
            uc.mem_write(a, b)
            dirty.append((a, len(b)))
        self.writes = []
        self.intr = None
        try:
            if case.branch:
                eip = case.seq[0].addr
                for i, ins in enumerate(case.seq):
                    uc.emu_start(eip, 0, timeout=2_000_000, count=1)
                    if self.intr is not None:
                        break
                    eip = uc.reg_read(XC.UC_X86_REG_EIP)
                    if i + 1 < len(case.seq) and eip != case.seq[i + 1].addr:
                        break
            else:
                eip = case.seq[0].addr
                for i, ins in enumerate(case.seq):
                    uc.emu_start(eip, ins.next, timeout=2_000_000)
                    if self.intr is not None:
                        break
                    eip = uc.reg_read(XC.UC_X86_REG_EIP)
        except UcError as e:
            res.invalid = str(e)
        if self.intr is not None:
            if self.intr == 0:
                res.trapped = True
            else:
                res.invalid = f'interrupt {self.intr}'
        res.exit = uc.reg_read(XC.UC_X86_REG_EIP)
        res.regs = [uc.reg_read(r) for r in self.gpr]
        ef = uc.reg_read(XC.UC_X86_REG_EFLAGS)
        res.flags = {n: (ef >> sh) & 1 for n, _, sh in FLAGS}
        res.flags['eflags'] = ef
        if st.fpu:
            sw = uc.reg_read(XC.UC_X86_REG_FPSW)
            res.fpu = {'top': (sw >> 11) & 7, 'cw': uc.reg_read(XC.UC_X86_REG_FPCW), 'sw': sw,
                       'c': [(sw >> 8) & 1, (sw >> 9) & 1, (sw >> 10) & 1, (sw >> 14) & 1],
                       'st': [tuple(uc.reg_read(r)) for r in self.fp]}
        # Changed bytes, then restore the pristine image.
        dirty += self.writes
        spans = sorted(dirty)
        merged = []
        for a, n in spans:
            if merged and a <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], a + n)
            else:
                merged.append([a, a + n])
        for a, end in merged:
            for base, data in self.world.regions:
                lo, hi = max(a, base), min(end, base + len(data))
                if lo >= hi:
                    continue
                cur = uc.mem_read(lo, hi - lo)
                ref = data[lo - base:hi - base]
                if cur != ref:
                    for i, (x, y) in enumerate(zip(cur, ref)):
                        if x != y:
                            res.mem[lo + i] = x
                    uc.mem_write(lo, bytes(ref))
        if self.intr is not None or res.invalid:
            uc.context_restore(self.clean)
        elif case.m in ('mov', 'pop', 'les', 'lds'):
            self.reset_segments()
        return res


# ------------------------------------------------------- state generation
def set_part(regs, name, value):
    i, sh, bits = SUBREG[name]
    mask = ((1 << bits) - 1) << sh
    regs[i] = (regs[i] & ~mask & MASK32) | ((value << sh) & mask)


def get_part(regs, name):
    i, sh, bits = SUBREG[name]
    return (regs[i] >> sh) & ((1 << bits) - 1)


def rand_val(rng, bits):
    mask = (1 << bits) - 1
    r = rng.random()
    if r < 0.35:
        return rng.getrandbits(bits)
    if r < 0.55:
        return rng.randrange(0, 17)
    if r < 0.70:
        return (-rng.randrange(1, 17)) & mask
    if r < 0.85:
        return (rng.choice(EDGES[bits]) + rng.randrange(-2, 3)) & mask
    return rng.getrandbits(rng.randrange(1, bits + 1))


def fpu_value(rng, kind=''):
    if kind == 'trig':
        r = rng.random()
        if r < 0.8:
            return rng.uniform(-10, 10)
        return rng.choice([0.0, -0.0, math.pi, -math.pi / 2, 1e6, 1e-9, 12345.678])
    if kind == 'log':
        if rng.random() < 0.85:
            return math.ldexp(rng.uniform(1, 2), rng.randrange(-30, 30))
        return rng.choice([1.0, 2.0, 0.5, 1e-300, 1e300])
    if kind == 'prem':
        return rng.uniform(-1e4, 1e4) if rng.random() < 0.8 else rng.choice([0.0, 7.0, -7.5, 1e9])
    if kind == 'divisor':
        return rng.choice([1, -1]) * math.ldexp(rng.uniform(1, 2), rng.randrange(-4, 8))
    r = rng.random()
    if r < 0.30:
        return rng.uniform(-1000, 1000)
    if r < 0.45:
        return float(rng.randrange(-100, 101))
    if r < 0.60:
        return rng.choice([0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.0, -0.0, 1.0, -1.0, 0.25, 3.0, 10.0,
                           0.1, 1 / 3, -0.49999999999999994, 4.5, -3.5])
    if r < 0.80:
        return math.ldexp(rng.uniform(1, 2), rng.randrange(-40, 40)) * rng.choice([1, -1])
    if r < 0.92:
        return rng.choice([2147483647.5, 2147483647.4, 2147483648.0, -2147483648.0, -2147483648.5,
                           -2147483648.6, 32767.5, 32767.4, 32768.0, -32768.0, -32768.5, -32768.6,
                           4294967296.0, 1e10, -1e10, 9.3e18, 65535.0, 123456789.5])
    return rng.choice([1e300, -1e300, 1e-300, 5e-324, 2.2250738585072014e-308, math.inf, -math.inf,
                       math.nan, 1.7976931348623157e308])


def fpu_kind(m):
    if m in ('fsin', 'fcos'):
        return 'trig'
    if m == 'fyl2x':
        return 'log'
    if m == 'fprem':
        return 'prem'
    return ''


class StateGen:
    def __init__(self, world, fpu_cw=0x127f):
        self.world = world
        self.fpu_cw = fpu_cw

    # -- memory operands
    def place(self, ins, op, st, locked, rng, size):
        from capstone import x86 as X
        mem = op.mem
        base = reg_name(ins, mem.base) if mem.base else None
        index = reg_name(ins, mem.index) if mem.index else None
        bi = SUBREG[base][0] if base else None
        ii = SUBREG[index][0] if index else None
        scale = mem.scale
        disp = mem.disp & MASK32
        seg = FS_BASE if mem.segment == X.X86_REG_FS else 0
        world = self.world
        absolute = seg or (bi is None and ii is None) or \
            (disp >= 0x10000 and disp < 0x80000000 and world.region_of(disp) is not None)
        for _ in range(40):
            regs = list(st.regs)
            if absolute:
                for r in (bi, ii):
                    if r is not None and r not in locked:
                        regs[r] = rng.choice([0, 0, rng.randrange(0, 16) * 4, rng.randrange(0, 8)])
            else:
                region = STACK if base in ('esp', 'ebp') else SCRATCH
                align = min(size, 4)
                target = region[0] + region[1] // 2 + rng.randrange(-0x3000, 0x3000)
                target -= target % align
                if ii is not None and ii != bi and ii not in locked:
                    regs[ii] = rng.randrange(0, 16)
                if bi is not None and bi not in locked:
                    if ii == bi:
                        rest = (target - disp) & MASK32
                        regs[bi] = (s32(rest) // (scale + 1)) & MASK32
                    else:
                        idx = regs[ii] * scale if ii is not None else 0
                        regs[bi] = (target - disp - idx) & MASK32
                elif ii is not None and ii not in locked:
                    b = regs[bi] if bi is not None else 0
                    regs[ii] = (s32((target - disp - b) & MASK32) // scale) & MASK32
            ea = (disp + (regs[bi] if bi is not None else 0) + (regs[ii] * scale if ii is not None else 0)) & MASK32
            ea = (ea + seg) & MASK32
            if world.region_of(ea, size) is not None and (not seg or FS_BASE <= ea and ea + size <= FS_BASE + FS_SIZE):
                st.regs = regs
                for r in (bi, ii):
                    if r is not None:
                        locked.add(r)
                return ea
        return None

    def gen(self, case, k, n_edge, rng, quick):
        from capstone import x86 as X
        ins, m = case.ins, case.m
        st = State()
        st.regs = [rng.getrandbits(32) for _ in range(8)]
        st.flags = rng.getrandbits(6)
        locked = {ESP}
        st.regs[ESP] = STACK[0] + 0x8000 + rng.randrange(-0x2000, 0x2000, 4)
        edge = k < n_edge
        ek = (QUICK_EDGES[k] if quick else k) if edge else None
        rep = raw_rep(ins) if m in STRING else None
        size = ins.ops[0].size if ins.ops else 4

        # implicit address registers
        if m in STRING:
            st.regs[ESI] = SCRATCH[0] + 0x6000 + rng.randrange(0, 0x1000)
            st.regs[EDI] = SCRATCH[0] + 0xc000 + rng.randrange(0, 0x1000)
            if rng.random() < 0.6:
                st.regs[ESI] &= ~3
                st.regs[EDI] &= ~3
            locked |= {ESI, EDI}
            if rep:
                st.regs[ECX] = [0, 1, 2, 3, 7, 16, 33][k % 7] if k < 7 else rng.randrange(0, 40)
                locked.add(ECX)
            if k % 2:
                st.flags |= DF
        elif m in ('std', 'cld', 'pushfd', 'popfd', 'lahf', 'sahf'):
            if rng.random() < 0.5:
                st.flags |= DF
        if m == 'leave':
            st.regs[EBP] = st.regs[ESP] + rng.randrange(0, 0x400, 4)
            locked.add(EBP)
        if m in ('loop', 'loope', 'loopne', 'jecxz'):
            st.regs[ECX] = [0, 1, 2, 0xffffffff, 0x80000000, 0x10000][k] if k < 6 else rand_val(rng, 32)
            locked.add(ECX)

        # explicit memory operands
        eas = {}
        if m not in STRING and m != 'lea':
            for n, o in enumerate(ins.ops):
                if o.type != X.X86_OP_MEM:
                    continue
                osize = o.size
                if m in ('fnsave', 'frstor', 'fsave'):
                    osize = 108
                elif m in ('les', 'lds'):
                    osize = 6
                ea = self.place(ins, o, st, locked, rng, osize)
                if ea is None:
                    return None
                eas[n] = (ea, osize)

        # memory operands of later instructions of a sequence, unless their
        # address registers are written earlier in the sequence
        written = set()
        for i, later in enumerate(case.seq):
            if i:
                for n, o in enumerate(later.ops):
                    if o.type != X.X86_OP_MEM:
                        continue
                    regs = {SUBREG[reg_name(later, r)][0] for r in (o.mem.base, o.mem.index) if r}
                    if regs & written:
                        continue
                    if self.place(later, o, st, locked, rng, o.size) is None:
                        return None
            try:
                written |= {SUBREG[later.cs.reg_name(r)][0] for r in later.cs.regs_access()[1]
                            if later.cs.reg_name(r) in SUBREG}
            except Exception:
                written |= set(range(8))

        # data operands
        datas = []
        for n, o in enumerate(ins.ops):
            if o.type == X.X86_OP_REG:
                name = reg_name(ins, o.reg)
                if name in SUBREG:
                    datas.append(('reg', name, o.size * 8))
            elif o.type == X.X86_OP_MEM and m == 'lea':
                for r in (o.mem.base, o.mem.index):
                    if r:
                        datas.append(('reg', reg_name(ins, r), 32))
            elif o.type == X.X86_OP_MEM and not case.fpu and n in eas and m not in ('les', 'lds'):
                datas.append(('mem', eas[n][0], o.size * 8))
        acc = {1: 'al', 2: 'ax', 4: 'eax'}
        if m in ('mul', 'div', 'idiv') or (m == 'imul' and len(ins.ops) == 1):
            datas.insert(0, ('reg', acc[size], size * 8))
            if size > 1:
                datas.insert(1, ('reg', {2: 'dx', 4: 'edx'}[size], size * 8))
        elif m in ('cwde', 'cwd'):
            datas.append(('reg', 'ax', 16))
        elif m == 'cbw' or m == 'aam' or m == 'xlatb':
            datas.append(('reg', 'al', 8))
        elif m == 'cdq':
            datas.append(('reg', 'eax', 32))
        elif m == 'sahf':
            datas.append(('reg', 'ah', 8))
        elif m in ('scasb', 'scasw', 'scasd', 'stosb', 'stosw', 'stosd'):
            n = {'b': 1, 'w': 2, 'd': 4}[m[-1]]
            datas.append(('reg', acc[n], n * 8))
        imm = next((o.imm for o in ins.ops if o.type == X.X86_OP_IMM), None)
        values = []
        for j, (kind, where, bits) in enumerate(datas):
            mask = (1 << bits) - 1
            if edge and j < 2:
                E = EDGES[bits]
                v = E[ek % len(E)] if j == 0 else E[(3 * ek + 5) % len(E)]
            elif j == 1 and values and rng.random() < 0.15:
                v = values[0] & mask
            else:
                v = rand_val(rng, bits)
            values.append(v)
        # The first random states put the operands on the relations that
        # decide branches: equal / zero result / immediate - 1 / + 1.
        r = k - n_edge
        if datas and 0 <= r < 3 and m in ('cmp', 'sub', 'sbb', 'add', 'adc', 'test', 'and', 'or', 'xor', 'inc', 'dec', 'neg'):
            bits = datas[0][2]
            mask = (1 << bits) - 1
            if imm is not None:
                if m in ('cmp', 'sub', 'sbb'):
                    values[0] = (imm + (0, -1, 1)[r]) & mask
                elif m in ('add', 'adc'):
                    values[0] = (-imm + (0, -1, 1)[r]) & mask
                elif m in ('test', 'and'):
                    values[0] = (~imm, imm, 0)[r] & mask
                else:
                    values[0] = (0, imm, ~imm)[r] & mask
            elif len(values) > 1:
                if m in ('add', 'adc'):
                    values[1] = (-values[0] + (0, -1, 1)[r]) & mask
                elif m in ('test', 'and'):
                    values[1] = (~values[0], values[0], 0)[r] & mask
                elif r == 1:
                    values[0] = values[1] = 0
                else:
                    values[1] = (values[0] + (0, 0, 1)[r]) & mask
            else:
                values[0] = ({'inc': mask, 'dec': 1}.get(m, 0), 1 << (bits - 1), (1 << (bits - 1)) - 1)[r]
        for (kind, where, bits), v in zip(datas, values):
            if kind == 'reg':
                if SUBREG[where][0] in locked:
                    continue
                set_part(st.regs, where, v)
            else:
                st.patches.append((where, (v & ((1 << bits) - 1)).to_bytes(bits // 8, 'little')))
        # Conditional readers (jcc, setcc, loope/loopne) go through every
        # combination of the flags they read.
        from tools.recomp import flags as F
        cc = F.cc_of(m)
        reads = F.CC_READS[cc] if cc else (ZF if m in ('loope', 'loopne') else 0)
        if reads:
            bits = [b for b in (CF, PF, ZF, SF, OF) if reads & b]
            combo = k % (1 << len(bits))
            for i, b in enumerate(bits):
                st.flags = (st.flags | b) if combo >> i & 1 else (st.flags & ~b)

        # shift and rotate counts in CL
        if (m in SHIFTS or m in ROTATES) and len(ins.ops) > 1 and ins.ops[1].type == X.X86_OP_REG \
                and ECX not in locked:
            cnt = SHIFT_COUNTS[k % len(SHIFT_COUNTS)] if k < len(SHIFT_COUNTS) else \
                (rng.choice(SHIFT_COUNTS) if rng.random() < 0.5 else rng.getrandbits(8))
            set_part(st.regs, 'cl', cnt)

        if m in ('div', 'idiv') and not edge and k % 3 != 2:
            self.craft_div(case, st, eas, rng, locked)
        self.special(case, st, eas, k, rng)
        if case.fpu:
            self.fpu_state(case, st, eas, k, rng)
        return st

    def craft_div(self, case, st, eas, rng, locked):
        from capstone import x86 as X
        ins = case.ins
        o = ins.ops[0]
        bits = o.size * 8
        signed = case.m == 'idiv'
        for _ in range(20):
            if signed:
                d = rng.choice([rng.randrange(1, 1 << (bits - 1)), rng.randrange(1, 17), 1 << (bits - 1)])
                d = d if d == 1 << (bits - 1) else d * rng.choice([1, -1])
                if d == 1 << (bits - 1):
                    d = -d
                q = rng.randrange(-(1 << (bits - 1)), 1 << (bits - 1))
                r = rng.randrange(0, abs(d))
                n = q * d
                if n < 0 or (n == 0 and rng.random() < 0.5):
                    r = -r
                n += r
                if not -(1 << (2 * bits - 1)) <= n < (1 << (2 * bits - 1)):
                    continue
                qq = abs(n) // abs(d) * (1 if (n < 0) == (d < 0) else -1)
                if not -(1 << (bits - 1)) <= qq < (1 << (bits - 1)):
                    continue
            else:
                d = rng.choice([rng.randrange(1, 1 << bits), rng.randrange(1, 17)])
                q = rng.randrange(0, 1 << bits)
                n = q * d + rng.randrange(0, d)
            mask = (1 << (2 * bits)) - 1
            n &= mask
            dv = d & ((1 << bits) - 1)
            if o.type == X.X86_OP_REG:
                name = reg_name(ins, o.reg)
                if SUBREG[name][0] in (EAX, EDX) or SUBREG[name][0] in locked:
                    return
                set_part(st.regs, name, dv)
            else:
                ea = eas[0][0]
                st.patches = [(a, b) for a, b in st.patches if a != ea]
                st.patches.append((ea, dv.to_bytes(bits // 8, 'little')))
            if bits == 8:
                set_part(st.regs, 'ax', n)
            elif bits == 16:
                set_part(st.regs, 'ax', n & 0xffff)
                set_part(st.regs, 'dx', n >> 16)
            else:
                st.regs[EAX] = n & MASK32
                st.regs[EDX] = n >> 32
            st.note = 'crafted non-faulting division'
            return

    def special(self, case, st, eas, k, rng):
        from capstone import x86 as X
        ins, m = case.ins, case.m
        ops = ins.ops
        sel_for = {'fs': SEL_FS}
        if m == 'mov' and ops[0].type == X.X86_OP_REG and reg_name(ins, ops[0].reg) in SEGREGS:
            seg = reg_name(ins, ops[0].reg)
            sel = sel_for.get(seg, SEL_DATA)
            if ops[1].type == X.X86_OP_REG:
                set_part(st.regs, reg_name(ins, ops[1].reg), sel)
            else:
                ea = eas[1][0]
                st.patches.append((ea, struct.pack('<H', sel)))
        if m == 'pop' and ops[0].type == X.X86_OP_REG and reg_name(ins, ops[0].reg) in SEGREGS:
            sel = sel_for.get(reg_name(ins, ops[0].reg), SEL_DATA)
            st.patches.append((st.regs[ESP], struct.pack('<I', sel | (rng.getrandbits(16) << 16))))
        if m in ('les', 'lds'):
            ea = eas[1][0]
            st.patches.append((ea, rng.randbytes(4) + struct.pack('<H', SEL_DATA)))
        if m == 'verr':
            set_part(st.regs, reg_name(ins, ops[0].reg), rng.choice([SEL_DATA, SEL_FS]))
        if m == 'popfd':
            v = 0x202 | (rng.getrandbits(12) & 0x8d5) | (rng.getrandbits(1) << 10)
            st.patches.append((st.regs[ESP], struct.pack('<I', v)))
        if m in ('scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd') and k % 4 in (1, 2):
            n = {'b': 1, 'w': 2, 'd': 4}[m[-1]]
            step = -n if st.flags & DF else n
            count = st.regs[ECX] if raw_rep(ins) else 1
            if count:
                p = rng.randrange(0, min(count, 40))
                if m.startswith('scas'):
                    val = st.regs[EAX] & ((1 << (8 * n)) - 1)
                    st.patches.append(((st.regs[EDI] + p * step) & MASK32, val.to_bytes(n, 'little')))
                else:
                    for i in range(p + (0 if k % 4 == 1 else 1)):
                        src = (st.regs[ESI] + i * step) & MASK32
                        dst = (st.regs[EDI] + i * step) & MASK32
                        st.patches.append((dst, self.world.pristine(src, n)))

    def fpu_state(self, case, st, eas, k, rng):
        from capstone import x86 as X
        ins, m = case.ins, case.m
        kind = fpu_kind(m)
        top = rng.randrange(8)
        vals = [widen(rng, fpu_value(rng)) for _ in range(8)]
        vals[top] = widen(rng, fpu_value(rng, kind))
        if kind == 'prem':
            vals[(top + 1) & 7] = widen(rng, fpu_value(rng, 'divisor'))
            if k % 5 == 4:                    # exponent difference 53..90: partial remainders
                vals[top] = widen(rng, math.ldexp(rng.uniform(1, 2), rng.randrange(53, 90)))
        elif kind == 'log':
            vals[(top + 1) & 7] = widen(rng, fpu_value(rng))
        cw = self.fpu_cw
        if m in FPU_RC:
            cw = (cw & ~0xc00) | (k % 4) << 10
        elif k % 3 == 2:
            cw = (cw & ~0xc00) | rng.randrange(1, 4) << 10     # directed rounding
        cbits = [rng.getrandbits(1) for _ in range(4)]
        if m in ('fnstsw', 'fstsw'):
            # sequences test TOP == 0 and C0/C3 (after sahf: CF/ZF)
            top = 0 if k % 8 < 2 else top
            cbits[0], cbits[3] = k & 1, (k >> 1) & 1
        mem = [(n, o) for n, o in enumerate(ins.ops) if o.type == X.X86_OP_MEM]
        if m in FPU_COMPARE and not mem:
            regs = [reg_name(ins, o.reg) for o in ins.ops if o.type == X.X86_OP_REG]
            j = int(regs[-1][3:-1]) if regs else (None if m == 'ftst' else 1)
            r = k % 4
            if r == 1:
                if j is None:
                    vals[top] = rng.choice([(0, 0), (0, 0x8000)])
                else:
                    vals[(top + j) & 7] = vals[top]
            elif r == 2 and rng.random() < 0.3:
                vals[top] = (0xc000000000000000, 0x7fff)
        st.fpu = {'top': top, 'cw': cw, 'sw': rng.getrandbits(6), 'c': cbits, 'st': vals}
        if not mem:
            return
        n, o = mem[0]
        ea, size = eas[n]
        integer = m.startswith('fi') and m not in ('fist', 'fistp')
        if m in ('fild',) or (integer and m != 'fild'):
            bits = size * 8
            if rng.random() < 0.6:
                v = EDGES.get(bits, EDGES[32])[k % 11] if bits <= 32 else rand_val(rng, 32)
            else:
                v = rand_val(rng, min(bits, 32))
            if bits == 64:
                v = rng.getrandbits(64) if rng.random() < 0.5 else s32(v) & ((1 << 64) - 1)
            st.patches.append((ea, (v & ((1 << bits) - 1)).to_bytes(size, 'little')))
        elif m == 'fldcw':
            cwv = rng.choice([0x027f, 0x037f, 0x0e7f, 0x0f7f, 0x067f, 0x0a7f, 0x0c7f, 0x007f, 0x0b7f,
                              0x127f, 0x1f7f])
            st.patches.append((ea, struct.pack('<H', cwv)))
        elif m == 'frstor':
            img = bytearray(108)
            rtop = rng.randrange(8)
            sw = rng.getrandbits(6) | rng.getrandbits(1) << 8 | rng.getrandbits(1) << 9 | \
                rng.getrandbits(1) << 10 | rtop << 11 | rng.getrandbits(1) << 14
            struct.pack_into('<III', img, 0, rng.choice([0x027f, 0x037f, 0x127f, 0x007f]) |
                             (rng.randrange(4) << 10), sw, 0)
            for i in range(8):
                img[28 + 10 * i:38 + 10 * i] = f80_bytes(*widen(rng, fpu_value(rng)))
            st.patches.append((ea, bytes(img)))
        elif m in ('fld', 'fadd', 'fsub', 'fsubr', 'fmul', 'fdiv', 'fdivr', 'fcom', 'fcomp',
                   'fucom', 'fucomp'):
            v = fpu_value(rng, 'divisor' if m in ('fdiv', 'fdivr') and rng.random() < 0.5 else '')
            same = False
            if m.startswith('fcom') or m.startswith('fucom'):
                r = k % 4
                if r == 1:
                    same = True
                elif r == 2:
                    v = math.nan if rng.random() < 0.3 else v * (1 + rng.choice([1, -1]) * 1e-9)
            if size == 4:
                try:
                    b = struct.pack('<f', v)
                except OverflowError:
                    b = struct.pack('<f', math.copysign(math.inf, v))
                if same:
                    st.fpu['st'][top] = f64_to_f80(struct.unpack('<f', b)[0])
            elif size == 8:
                b = struct.pack('<d', v)
                if same:
                    st.fpu['st'][top] = f64_to_f80(v)
            else:
                t = widen(rng, v, 0.35)
                if same:
                    st.fpu['st'][top] = t
                b = f80_bytes(*t)
            st.patches.append((ea, b))


def widen(rng, x, p=0.4):
    """80-bit register value from x: often with a full 64-bit mantissa, now and
    then with an exponent beyond the double range or as an extended denormal."""
    m, se = f64_to_f80(x)
    if (se & 0x7fff) == 0x7fff or m == 0:
        return m, se
    r = rng.random()
    if r < p:
        m |= rng.getrandbits(11)
    elif r < p + 0.04:
        se = (se & 0x8000) | rng.choice([0x0001, 0x2000, 0x3a00, 0x4500, 0x6000, 0x7ffe])
    elif r < p + 0.05:
        se &= 0x8000
        m = rng.getrandbits(63) | 1
    return m, se


# ------------------------------------------------------------- compare
def undefined_flags(case, st):
    """Flags whose value the SDM leaves undefined after the case ran from st."""
    from tools.recomp import flags as F
    u = 0
    for ins in case.seq:
        _, d, _ = F.use_def(ins)
        u = (u & ~d) | insn_undefined(ins, st)
    return u


def insn_undefined(ins, st):
    from capstone import x86 as X
    m = ins.mnem
    if m in ('and', 'or', 'xor', 'test'):
        return AF
    if m in SHIFTS or m in ROTATES:
        if len(ins.ops) < 2:
            cnt = 1
        elif ins.ops[1].type == X.X86_OP_IMM:
            cnt = ins.ops[1].imm & 31
        else:
            cnt = st.regs[ECX] & 31
        if cnt == 0:
            return 0
        bits = ins.ops[0].size * 8
        u = 0 if cnt == 1 else OF
        if m in SHIFTS:
            u |= AF
            if m in ('shl', 'sal', 'shr') and cnt >= bits:
                u |= CF | OF
        return u
    if m in ('mul', 'imul'):
        return SF | ZF | AF | PF
    if m in ('div', 'idiv'):
        return STATUS
    if m == 'aam':
        return OF | AF | CF
    if m in ('bt', 'bts', 'btr', 'btc'):
        return OF | SF | AF | PF
    if m in ('bsf', 'bsr'):
        return CF | OF | SF | AF | PF
    return 0


def f80_rel(a, b):
    """Relative difference of two 80-bit values (0 if equal, inf if not comparable)."""
    if a == b or (f80_isnan(a) and f80_isnan(b)):
        return 0.0
    if f80_isnan(a) or f80_isnan(b) or (a[1] & 0x7fff) == 0x7fff or (b[1] & 0x7fff) == 0x7fff:
        return math.inf
    x, y = f80_fraction(*a), f80_fraction(*b)
    return float(abs(x - y) / max(abs(x), abs(y)))


def f80_exponent(t):
    m, se = t
    return (se & 0x7fff or 1) - 16383 + m.bit_length() - 64


def fprem_reference(a, b):
    """Exact complete fprem: (remainder, quotient & 7), or None when the operands
    are not finite and nonzero or the x87 would leave a partial remainder."""
    finite = all((t[1] & 0x7fff) != 0x7fff for t in (a, b))
    if not finite or b[0] == 0:
        return None
    if a[0] == 0:
        return a, 0
    if f80_exponent(a) - f80_exponent(b) >= 64:
        return None
    x, y = f80_fraction(*a), f80_fraction(*b)
    n = abs(x / y).__floor__() * (1 if (x < 0) == (y < 0) else -1)
    return fraction_to_f80(x - n * y, neg_zero=bool(a[1] >> 15)), abs(n) & 7


def compare(case, st, cr, ur, stats):
    """Returns (errors, known): lists of mismatch descriptions."""
    errs, known = [], []
    if cr.trapped or ur.trapped:
        if cr.trapped != ur.trapped:
            errs.append(f'trap: C={cr.trapped} unicorn={ur.trapped}')
        return errs, known
    if cr.exit != ur.exit:
        errs.append(f'exit: C={cr.exit:08x} unicorn={ur.exit:08x}')
    for i, n in enumerate(REG32):
        if cr.regs[i] != ur.regs[i]:
            errs.append(f'{n}: C={cr.regs[i]:08x} unicorn={ur.regs[i]:08x}')
    if case.kind == 'insn':
        mask = (STATUS | DF) & ~undefined_flags(case, st)
    else:
        live = case.exit_live.get(cr.exit) if case.exit_live else None
        mask = DF | ((live if live is not None else 0) & ~undefined_flags(case, st))
    for n, bit, _ in FLAGS:
        cv = cr.flags[n]
        if cv not in (0, 1):
            errs.append(f'{n}: C value {cv} is not 0/1')
        elif bit & mask and cv != ur.flags[n]:
            errs.append(f'{n}: C={cv} unicorn={ur.flags[n]}')
    if cr.mem != ur.mem:
        addrs = sorted(set(cr.mem) | set(ur.mem))
        bad = [a for a in addrs if cr.mem.get(a) != ur.mem.get(a)]
        a = bad[0]

        def show(d):
            return f'{d[a]:02x}' if a in d else 'unchanged'
        errs.append(f'memory: {len(bad)} byte(s) differ, first at {a:08x}: C={show(cr.mem)} unicorn={show(ur.mem)}')
    if case.fpu and st.fpu:
        m = case.m
        cf, uf = cr.fpu, ur.fpu
        if cf['top'] != uf['top']:
            errs.append(f'fpu TOP: C={cf["top"]} unicorn={uf["top"]}')
        top = st.fpu['top']
        done = set()                      # registers checked against another reference
        cref = {}                         # C bits checked against another reference
        uc_partial = False
        if m in HOST_REF and case.kind == 'insn':
            # The four instructions unicorn computes in doubles or inexactly are
            # compared bit for bit with the host x87 running the same instruction.
            ri = (top + (1 if m == 'fyl2x' else 0)) & 7
            if cf['st'][ri] != cf['ref']:
                errs.append(f'ST(0) vs host x87: C={fmt80(cf["st"][ri])} x87={fmt80(cf["ref"])}')
            bits = (0, 1, 2, 3) if m == 'fprem' else (2,) if m in ('fsin', 'fcos') else ()
            for bit in bits:
                cref[bit] = (cf['ref_sw'] >> (8, 9, 10, 14)[bit]) & 1
            rel = f80_rel(cf['st'][ri], uf['st'][ri])
            if rel:
                key = f'{m} (unicorn vs x87)'
                if rel != math.inf:
                    stats['fpu_maxrel'][key] = max(stats['fpu_maxrel'].get(key, 0.0), rel)
                if rel > 1e-13:
                    known.append(f'ST(0): unicorn {fmt80(uf["st"][ri])}, C and host x87 {fmt80(cf["st"][ri])}')
            done.add(ri)
        if m == 'fprem':
            a, b = st.fpu['st'][top], st.fpu['st'][(top + 1) & 7]
            ref = fprem_reference(a, b)
            if ref is not None:
                (rem, q) = ref
                if rem is None or cf['st'][top] != rem:
                    errs.append(f'ST(0): C={fmt80(cf["st"][top])} exact remainder={rem and fmt80(rem)}')
                for bit, v in ((0, q >> 2 & 1), (3, q >> 1 & 1), (1, q & 1), (2, 0)):
                    if cf['c'][bit] != v:
                        errs.append(f'fpu C{bit}: C={cf["c"][bit]} exact={v}')
                    cref.setdefault(bit, v)
                if uf['st'][top] != rem:
                    known.append(f'ST(0): unicorn fprem {fmt80(uf["st"][top])} is inexact; '
                                 f'C and the exact remainder give {fmt80(rem)}')
                done.add(top)
            if all((t[1] & 0x7fff) != 0x7fff and t[0] for t in (a, b)) and \
                    f80_exponent(a) - f80_exponent(b) >= 53:
                uc_partial = True         # QEMU 5 reduces partially from 53 bits on
        if m in FPU_CONST:
            down, se, up = FPU_CONST[m]
            rc = (st.fpu['cw'] >> 10) & 3
            want = (down + (1 if rc == 2 else up if rc == 0 else 0), se)
            ri = (top - 1) & 7
            if cf['st'][ri] != want:
                errs.append(f'ST(0): C={fmt80(cf["st"][ri])} x87 constant for RC={rc}: {fmt80(want)}')
            elif uf['st'][ri] != want:
                known.append(f'ST(0): unicorn ignores RC={rc} for {m}: {fmt80(uf["st"][ri])}, C {fmt80(want)}')
            done.add(ri)
        if m == 'fyl2x' and f80_to_f64(*st.fpu['st'][top]) <= 0:
            uc_partial = True             # QEMU 5 refuses ST0 <= 0 in double precision: no pop
        if m not in FPU_INIT:
            for i in range(8):
                if i in done:
                    continue
                if cf['st'][i] != uf['st'][i]:
                    sti = (i - uf['top']) & 7
                    msg = f'ST({sti}) [phys {i}]: C={fmt80(cf["st"][i])} unicorn={fmt80(uf["st"][i])}'
                    if m in FPU_TRANSC:
                        rel = f80_rel(cf['st'][i], uf['st'][i])
                        if rel <= 1e-13:
                            stats['fpu_maxrel'][f'{m} (unicorn)'] = max(
                                stats['fpu_maxrel'].get(f'{m} (unicorn)', 0.0), rel)
                            continue
                    errs.append(msg)
            if not errs and any(not f80_is_double(*t) for t in uf['st']):
                stats['x87_extended'][m] = stats['x87_extended'].get(m, 0) + 1
        cbits = []
        if m in FPU_COMPARE:
            cbits = [0, 2, 3]
        elif m in ('fsin', 'fcos', 'fprem'):
            cbits = [2]
        elif m in FPU_INIT or m == 'frstor':
            cbits = [0, 1, 2, 3]
        for bit in set(cbits) | set(cref):
            want = cref.get(bit, uf['c'][bit])
            if cf['c'][bit] != want:
                errs.append(f'fpu C{bit}: C={cf["c"][bit]} expected={want} unicorn={uf["c"][bit]}')
            elif bit in cref and uf['c'][bit] != want and bit in cbits:
                known.append(f'fpu C{bit}: unicorn={uf["c"][bit]}, C and reference={want}')
        if m in ('fldcw', 'frstor') or m in FPU_INIT:
            if cf['cw'] != uf['cw']:
                errs.append(f'fpu CW: C={cf["cw"]:04x} unicorn={uf["cw"]:04x}')
        if m in FPU_INIT or m == 'frstor':
            if (cf['sw'] & 0x3f) != (uf['sw'] & 0x3f):
                errs.append(f'fpu SW exceptions: C={cf["sw"]:04x} unicorn={uf["sw"]:04x}')
        host_ok = case.kind == 'insn' and m in HOST_REF and not any('host x87' in e for e in errs)
        if uc_partial and errs and (case.kind != 'insn' or host_ok and m == 'fyl2x'):
            # unicorn takes another path: a partial fprem remainder from 53 bits of
            # exponent difference, or fyl2x of an operand that is <= 0 as a double
            known += [e + f' (unicorn {m} deviates; C matches the host x87)' for e in errs]
            errs = []
    return errs, known


# ------------------------------------------------------------- main
def run(args):
    t0 = time.time()
    world = World(args.seed)
    t_disc = time.time() - t0
    cases, sizes, skipped = select_cases(world, args.quick, args.seed, args.only)
    seq_cases, seq_sizes, seq_lifter = ([], {}, None) if args.no_seq else \
        select_sequences(world, args.quick, args.seed, args.only)
    all_cases = cases + seq_cases
    build_bodies(world, all_cases, seq_lifter)
    for c in seq_cases:
        if c.error is None:
            exits = {c.seq[-1].next}
            for i in c.seq:
                if i.flow == 'jcc':
                    exits.add(i.targets[0])
            c.exit_live = {e: live_in(world, c.func, e) for e in exits}
    workdir = Path(args.out) if args.out else OUT_DIR / ('quick' if args.quick else 'full')
    exe = build_binary(all_cases, workdir, args.jobs)
    t_build = time.time() - t0 - t_disc

    gen = StateGen(world, args.fpu_cw)
    n_edge = len(QUICK_EDGES) if args.quick else 11
    n_rand = args.states if args.states is not None else (4 if args.quick else 9)
    jobs = []
    gen_failed = collections.Counter()
    for c in all_cases:
        if c.error is not None:
            continue
        rng = random.Random(f'{args.seed}:{c.kind}:{c.addr:08x}:{len(c.seq)}')
        for k in range(n_edge + n_rand):
            st = gen.gen(c, k, n_edge, rng, args.quick)
            if st is None:
                gen_failed[c.addr] += 1
                continue
            jobs.append((c, st))
    c_results = run_c(exe, workdir, world, jobs)
    t_c = time.time() - t0 - t_disc - t_build

    oracle = Oracle(world)
    stats = {'fpu_maxrel': {}, 'x87_extended': {}}
    per_case = collections.defaultdict(lambda: {'states': 0, 'fail': 0, 'invalid': 0, 'known': 0, 'exits': set(),
                                                'first': None, 'known_first': None, 'msgs': collections.Counter()})
    for (c, st), cr in zip(jobs, c_results):
        ur = oracle.run(c, st)
        rec = per_case[id(c)]
        rec['states'] += 1
        if ur.invalid:
            rec['invalid'] += 1
            if rec.get('invalid_first') is None:
                rec['invalid_first'] = (st.describe(), ur.invalid)
            continue
        errs, known = compare(c, st, cr, ur, stats)
        if not ur.trapped:
            rec['exits'].add(ur.exit)
        if errs:
            rec['fail'] += 1
            for e in errs:
                rec['msgs'][e.split(':')[0]] += 1
            if rec['first'] is None:
                rec['first'] = (st.describe(), cr.describe(c), ur.describe(c), errs)
        elif known:
            rec['known'] += 1
            if rec['known_first'] is None:
                rec['known_first'] = (st.describe(), known)
    t_uc = time.time() - t0 - t_disc - t_build - t_c

    # ---- report
    groups = collections.OrderedDict()
    for c in all_cases:
        g = groups.setdefault(c.group, {'members': (sizes if c.kind == 'insn' else seq_sizes).get(c.group, 0),
                                        'tested': 0, 'pass': 0, 'fail': 0, 'unsupported': 0,
                                        'states': 0, 'failed_states': 0, 'invalid_states': 0,
                                        'known_states': 0})
        if c.error is not None:
            g['unsupported'] += 1
            continue
        rec = per_case[id(c)]
        g['tested'] += 1
        g['states'] += rec['states']
        g['failed_states'] += rec['fail']
        g['invalid_states'] += rec['invalid']
        g['known_states'] += rec['known']
        if rec['fail'] or rec['states'] - rec['invalid'] == 0:
            g['fail'] += 1
        else:
            g['pass'] += 1
    failures, unsupported, knowns, invalids = [], [], [], []
    for c in all_cases:
        base = {'kind': c.kind, 'group': c.group, 'addr': f'{c.addr:08x}', 'text': c.text,
                'bytes': ' '.join(bytes(i.cs.bytes).hex() for i in c.seq)}
        if c.error is not None:
            unsupported.append(dict(base, error=c.error))
            continue
        rec = per_case[id(c)]
        if rec['first']:
            stt, crd, urd, errs = rec['first']
            failures.append(dict(base, states=rec['states'], failed=rec['fail'], state=stt, c=crd,
                                 unicorn=urd, errors=errs, kinds=dict(rec['msgs'])))
        if rec['known_first']:
            knowns.append(dict(base, states=rec['known'], state=rec['known_first'][0],
                               notes=rec['known_first'][1]))
        if rec['states'] and rec['invalid'] == rec['states']:
            invalids.append(dict(base, state=rec['invalid_first'][0], error=rec['invalid_first'][1]))
    tested = [c for c in all_cases if c.error is None]
    branchy = [c for c in tested if c.branch]
    one_sided = [{'kind': c.kind, 'group': c.group, 'addr': f'{c.addr:08x}', 'text': c.text,
                  'exits': [f'{x:08x}' for x in sorted(per_case[id(c)]['exits'])]}
                 for c in branchy if len(per_case[id(c)]['exits']) < 2]
    summary = {
        'mode': 'quick' if args.quick else 'full',
        'fpu_cw': f'{args.fpu_cw:#06x}',
        'seed': args.seed,
        'instructions_discovered': sum(sizes.values()) + sum(skipped.values()),
        'instructions_testable': sum(sizes.values()),
        'skipped_by_kind': dict(sorted(skipped.items())),
        'groups': len(sizes),
        'cases_insn': sum(1 for c in tested if c.kind == 'insn'),
        'cases_seq': sum(1 for c in tested if c.kind == 'seq'),
        'cases_fseq': sum(1 for c in tested if c.kind == 'fseq'),
        'branch_cases': len(branchy),
        'branch_cases_one_outcome': len(one_sided),
        'chains_total': sum(seq_sizes.values()),
        'states': len(jobs),
        'states_failed': sum(r['fail'] for r in per_case.values()),
        'states_invalid': sum(r['invalid'] for r in per_case.values()),
        'states_known': sum(r['known'] for r in per_case.values()),
        'state_generation_failures': sum(gen_failed.values()),
        'unsupported': len(unsupported),
        'cases_failed': len(failures),
        'fpu_max_relative_error': {k: v for k, v in sorted(stats['fpu_maxrel'].items())},
        'x87_exact_states_with_non_double_values': dict(sorted(stats['x87_extended'].items())),
        'seconds': {'discover': round(t_disc, 1), 'build': round(t_build, 1), 'c': round(t_c, 1),
                    'unicorn': round(t_uc, 1)},
    }
    report = {'summary': summary, 'groups': groups, 'failures': failures, 'unsupported': unsupported,
              'known_differences': knowns, 'all_states_invalid': invalids, 'branch_one_outcome': one_sided}
    (workdir / 'report.json').write_text(json.dumps(report, indent=1) + '\n')

    w = max(len(k) for k in groups) if groups else 10
    print(f'{"group":<{w}} {"size":>6} {"tested":>6} {"pass":>5} {"fail":>5} {"states":>7} {"bad":>5} {"inv":>5} {"known":>5}')
    for k, g in groups.items():
        flag = '' if not g['fail'] and not g['unsupported'] else '  <--'
        print(f'{k:<{w}} {g["members"]:>6} {g["tested"]:>6} {g["pass"]:>5} {g["fail"]:>5} {g["states"]:>7} '
              f'{g["failed_states"]:>5} {g["invalid_states"]:>5} {g["known_states"]:>5}{flag}')
    print()
    for fr in failures[:args.show]:
        print(f'FAIL [{fr["group"]}] {fr["addr"]}: {fr["text"]}   ({fr["bytes"]})  {fr["failed"]}/{fr["states"]} states')
        print(f'   state:   {fr["state"]}')
        print(f'   C:       {fr["c"]}')
        print(f'   unicorn: {fr["unicorn"]}')
        for e in fr['errors'][:8]:
            print(f'   - {e}')
    if len(failures) > args.show:
        print(f'... {len(failures) - args.show} more failing cases in {workdir / "report.json"}')
    for u in unsupported:
        print(f'UNSUPPORTED [{u["group"]}] {u["addr"]}: {u["text"]}: {u["error"]}')
    for iv in invalids:
        print(f'NO VALID STATE [{iv["group"]}] {iv["addr"]}: {iv["text"]}: {iv["error"]}')
    print()
    for k, v in summary.items():
        if k not in ('skipped_by_kind',):
            print(f'{k}: {v}')
    ok = not failures and not invalids
    print('RESULT:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--quick', action='store_true', help='5 instructions per group, 8 states each')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--only', help='regex on the group key (e.g. "^shl|^fdiv")')
    ap.add_argument('--states', type=int, help='random states per case (in addition to edge states)')
    ap.add_argument('--no-seq', action='store_true', help='skip fused producer/consumer chains')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--fpu-cw', type=lambda v: int(v, 0), default=0x127f,
                    help='x87 control word of the initial states (default 0x127f: 53-bit precision, what '
                         'Watcom\'s 8087 init at 0x44692a loads; 0x37f: 64-bit, as in 0x44c47c)')
    ap.add_argument('--show', type=int, default=40, help='failures to print')
    ap.add_argument('--out', help='work directory (default build/recomp/unicorn_diff/{quick,full})')
    args = ap.parse_args(argv)
    missing = missing_requirements()
    if missing:
        print('missing requirements: ' + ', '.join(missing), file=sys.stderr)
        return 2
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
