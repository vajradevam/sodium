#!/bin/bash
# Deterministic mutation smoke test: the compiler must never crash or hang on
# malformed input. Exit 0 or 1 (with a diagnostic) are the only allowed outcomes.
# Usage: scripts/fuzz_smoke.sh [seed] [iterations]
set -u
cd "$(dirname "$0")/.."

COMPILER="${COMPILER:-./build/sodium}"
SEED="${1:-${SEED:-20261121}}"
N="${2:-${N:-400}}"

exec python3 - "$COMPILER" "$SEED" "$N" <<'PY'
import glob, os, random, subprocess, sys, tempfile, shutil

compiler, seed, n = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
corpus = sorted(glob.glob("tests/**/*.cyan", recursive=True))
assert corpus, "no corpus files found"
rng = random.Random(seed)

work = tempfile.mkdtemp(prefix="fuzz_", dir=os.environ.get("TMPDIR", "/tmp/opencode"))
crash_dir = "build/fuzz_crashes"
crashes = []
checked = 0


def mutate(data):
    op = rng.randrange(6)
    if not data:
        return b"function main(){"
    if op == 0:                                   # truncate
        return data[:rng.randrange(len(data))]
    if op == 1:                                   # delete a byte range
        a = rng.randrange(len(data))
        return data[:a] + data[a + rng.randrange(1, 32):]
    if op == 2:                                   # flip bytes
        b = bytearray(data)
        for _ in range(rng.randrange(1, 5)):
            i = rng.randrange(len(b))
            b[i] = rng.randrange(256)
        return bytes(b)
    if op == 3:                                   # insert junk
        a = rng.randrange(len(data) + 1)
        junk = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 24)))
        return data[:a] + junk + data[a:]
    if op == 4:                                   # drop a line
        lines = data.split(b"\n")
        if len(lines) > 1:
            del lines[rng.randrange(len(lines))]
        return b"\n".join(lines)
    a = rng.randrange(len(data))                  # duplicate a chunk
    length = rng.randrange(1, min(64, len(data) - a) + 1)
    return data[:a] + data[a:a + length] + data[a:]


src_path = os.path.join(work, "mutant.cyan")
asm_path = os.path.join(work, "out.s")
for i in range(n):
    data = mutate(open(rng.choice(corpus), "rb").read())
    with open(src_path, "wb") as f:
        f.write(data)
    target = ["x86_64", "riscv64"][i % 2]
    mode = [["-S"], ["--emit-ir", "-S"]][i % 2]
    cmd = [compiler, "--target", target] + mode + ["-I", "tests/include",
                                                    "-o", asm_path, src_path]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE, timeout=5)
        rc = proc.returncode
    except subprocess.TimeoutExpired:
        rc = "TIMEOUT"
    checked += 1
    if rc not in (0, 1):
        os.makedirs(crash_dir, exist_ok=True)
        kept = os.path.join(crash_dir, f"crash_seed{seed}_{i}.cyan")
        shutil.copyfile(src_path, kept)
        crashes.append((i, target, rc, kept, proc.stderr[:200] if rc != "TIMEOUT" else b""))

print(f"Fuzz smoke: seed={seed} iterations={n} checked={checked} crashes={len(crashes)}")
for i, target, rc, kept, err in crashes[:20]:
    print(f"  #{i} [{target}] exit={rc} input={kept} stderr={err!r}")
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if crashes else 0)
PY
