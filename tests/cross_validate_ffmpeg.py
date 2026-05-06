#!/usr/bin/env python3
"""
Cross-validate cubelut output against ffmpeg lut3d filter.
Tests all .cube files against multiple input images.
Computes per-pixel RMSE and max absolute error (8-bit scale).
Pass threshold: RMSE < 1.0  (identical encoding → 0.0000)
"""

import subprocess, os, sys, math

CUBELUT_BIN  = "/Users/2342184/programs/cubelut/build/apply_lut"
FFMPEG       = "ffmpeg"
CUBE_DIR     = "/Users/2342184/programs/cubelut/tests/files"
WORK_DIR     = "/tmp/cubelut_crossval"

INPUT_IMAGES = [
    ("/tmp/test_input.png", "gradient"),
    ("/tmp/test_hald.png",  "hald"),
    ("/tmp/test_noise.png", "noise"),
]

os.makedirs(WORK_DIR, exist_ok=True)

# ── helpers ──────────────────────────────────────────────────────────────────

def read_raw(path):
    """Decode PNG → flat bytes (rgb24) via ffmpeg rawvideo pipe."""
    r = subprocess.run(
        [FFMPEG, "-y", "-i", path, "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True
    )
    return r.stdout  # bytes, length = W*H*3

def rmse_max(a: bytes, b: bytes):
    """RMSE and max-abs-error between two rgb24 byte strings."""
    assert len(a) == len(b), f"size mismatch {len(a)} vs {len(b)}"
    sq, mx = 0.0, 0
    for x, y in zip(a, b):
        d = abs(int(x) - int(y))
        sq += d * d
        if d > mx: mx = d
    return math.sqrt(sq / len(a)), mx

# ── main ──────────────────────────────────────────────────────────────────────

cube_files = sorted(f for f in os.listdir(CUBE_DIR) if f.endswith(".cube"))

print(f"cubelut     : {CUBELUT_BIN}")
print(f"Cube files  : {len(cube_files)}  |  Images: {len(INPUT_IMAGES)}")
print("=" * 84)
print(f"  {'LUT file':<44}  {'image':<9}  {'RMSE':>8}  {'MaxErr':>7}  Status")
print("-" * 84)

all_pass = True

for cube in cube_files:
    cube_path = os.path.join(CUBE_DIR, cube)
    for img_path, label in INPUT_IMAGES:
        if not os.path.exists(img_path):
            print(f"  [SKIP] {img_path} not found")
            continue

        tag = f"{cube}_{label}"
        cubelut_out = os.path.join(WORK_DIR, f"cubelut_{tag}.png")
        ffmpeg_out  = os.path.join(WORK_DIR, f"ffmpeg_{tag}.png")

        # cubelut
        r1 = subprocess.run(
            [CUBELUT_BIN, cube_path, img_path, cubelut_out],
            capture_output=True, text=True
        )
        if r1.returncode != 0:
            print(f"  [ERR] cubelut: {r1.stderr.strip()[-80:]}")
            all_pass = False
            continue

        # ffmpeg lut3d
        r2 = subprocess.run(
            [FFMPEG, "-y", "-i", img_path, "-vf", f"lut3d={cube_path}", ffmpeg_out],
            capture_output=True, text=True
        )
        if r2.returncode != 0:
            print(f"  [ERR] ffmpeg: {r2.stderr.strip()[-120:]}")
            all_pass = False
            continue

        # pixel-level comparison
        raw_cl = read_raw(cubelut_out)
        raw_ff = read_raw(ffmpeg_out)
        rmse, mx = rmse_max(raw_cl, raw_ff)

        passed = rmse < 1.0
        if not passed:
            all_pass = False
        mark = "✅ PASS" if passed else "❌ FAIL"
        short = cube[:43]
        print(f"  {short:<43}  {label:<9}  {rmse:>8.4f}  {mx:>7.1f}  {mark}")

print("=" * 84)
print("ALL PASS ✅" if all_pass else "SOME FAILURES ❌")
sys.exit(0 if all_pass else 1)
