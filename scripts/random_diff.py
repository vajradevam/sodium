#!/usr/bin/env python3
"""T-7: deterministic differential testing of Cyan against gcc.

Generates random integer-only programs that are simultaneously valid C and
Cyan, compiles the C with gcc (reference), the Cyan with sodium for x86_64 and
riscv64, and compares exit status and stdout. Any mismatch is saved as an xfail
test under tests/xfail/ with the gcc output as the expectation.

The grammar deliberately avoids constructs with known bugs or implementation-
defined behaviour (narrow signed casts, u64, INT64_MIN/-1, shifts >= 32,
division by zero, deep nesting) so that a mismatch means a new finding.

Usage: scripts/random_diff.py [--seed N] [--count N] [--opt O0|O1]
"""
import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def gen_expr(rng, vars_, depth):
    if depth <= 0:
        if vars_ and rng.random() < 0.55:
            return rng.choice(vars_)
        return str(rng.randint(-20, 20))

    kind = rng.choice(['bin', 'bin', 'bin', 'unary', 'ternary', 'div'])
    if kind == 'bin':
        a = gen_expr(rng, vars_, depth - 1)
        b = gen_expr(rng, vars_, depth - 1)
        op = rng.choice(['+', '-', '*', '&', '|', '^', '<<', '>>',
                         '==', '!=', '<', '<=', '>', '>='])
        if op in ('<<', '>>'):
            b = str(rng.randint(0, 8))
        if op == '*':
            b = str(rng.randint(-8, 8))
        return '(' + a + ' ' + op + ' ' + b + ')'
    if kind == 'unary':
        op = rng.choice(['-', '!', '~'])
        # Parenthesise the operand so a negative literal cannot form '--9'
        # (which C parses as a decrement) or a similar token-pair surprise.
        return '(' + op + '(' + gen_expr(rng, vars_, depth - 1) + '))'
    if kind == 'ternary':
        a = gen_expr(rng, vars_, depth - 1)
        b = gen_expr(rng, vars_, depth - 1)
        c = gen_expr(rng, vars_, depth - 1)
        return '(' + a + ' ? ' + b + ' : ' + c + ')'
    a = gen_expr(rng, vars_, depth - 1)
    op = rng.choice(['/', '%'])
    b = str(rng.randint(1, 9))
    return '(' + a + ' ' + op + ' ' + b + ')'


def make_program(seed, nstmts=12):
    rng = random.Random(seed)
    vars_ = []
    cyan = ['function main() {']
    c = ['#include <stdio.h>', 'int main(void) {']
    for i in range(nstmts):
        expr = gen_expr(rng, vars_, 3)
        name = 'v' + str(i)
        cyan.append('    var ' + name + ' = ' + expr + ';')
        c.append('    long long ' + name + ' = ' + expr + ';')
        vars_.append(name)
    for name in vars_:
        cyan.append('    print(' + name + ');')
        c.append('    printf("%lld\\n", ' + name + ');')
    cyan.append('    return(0);')
    c.append('    return 0;')
    cyan.append('}')
    c.append('}')
    return '\n'.join(cyan) + '\n', '\n'.join(c) + '\n'


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=20, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seed', type=int, default=20261123)
    ap.add_argument('--count', type=int, default=24)
    ap.add_argument('--opt', choices=['O0', 'O1'], default='O1')
    ap.add_argument('--save-dir', default=os.path.join(ROOT, 'tests', 'xfail'))
    args = ap.parse_args()

    work = tempfile.mkdtemp(prefix='random_diff_',
                            dir=os.environ.get('TMPDIR', '/tmp/opencode'))
    sodium = os.path.join(ROOT, 'build', 'sodium')
    mismatches = 0
    try:
        src_cyan = os.path.join(work, 'prog.cyan')
        src_c = os.path.join(work, 'prog.c')
        for i in range(args.count):
            seed = args.seed + i
            cy, c = make_program(seed)
            with open(src_cyan, 'w') as f:
                f.write(cy)
            with open(src_c, 'w') as f:
                f.write(c)

            ref_bin = os.path.join(work, 'ref')
            cp = run(['gcc', '-O0', '-o', ref_bin, src_c])
            if cp.returncode != 0:
                print('program %d: gcc failed: %s' % (i, cp.stderr[:200].decode(errors='replace')))
                continue
            ref = run([ref_bin])
            want = (ref.returncode, ref.stdout)

            for target, runner in (('x86_64', []), ('riscv64', ['qemu-riscv64'])):
                bin_path = os.path.join(work, 'prog.' + target)
                tflag = [] if target == 'x86_64' else ['--target', 'riscv64']
                cp = run([sodium, '-' + args.opt, '-w'] + tflag +
                         ['-o', bin_path, src_cyan], cwd=ROOT)
                if cp.returncode != 0:
                    print('program %d [%s]: sodium compile failed: %s' %
                          (i, target, cp.stderr[:200].decode(errors='replace')))
                    mismatches += 1
                    continue
                try:
                    got = run(runner + [bin_path])
                    actual = (got.returncode, got.stdout)
                except subprocess.TimeoutExpired:
                    actual = ('TIMEOUT', b'')
                if actual != want:
                    mismatches += 1
                    name = 'diff_s%d_%d_%s.cyan' % (args.seed, i, target)
                    expected_lines = want[1].decode(errors='replace').splitlines()
                    header = ['// Differential mismatch vs gcc reference (T-7)',
                              '// seed=%d program=%d target=%s opt=%s' %
                              (args.seed, i, target, args.opt),
                              '// EXPECT-EXIT: %d' % want[0]]
                    header += ['// EXPECT-STDOUT: ' + line for line in expected_lines]
                    path = os.path.join(args.save_dir, name)
                    with open(path, 'w') as f:
                        f.write('\n'.join(header) + '\n\n' + cy)
                    print('program %d [%s]: MISMATCH -> %s (want %r, got %r)' %
                          (i, target, path, want, actual))
        print('Differential: %d programs, %d mismatches' % (args.count, mismatches))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if mismatches else 0


if __name__ == '__main__':
    sys.exit(main())
