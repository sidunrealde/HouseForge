"""Measure what HF_BUMP_HLSL's finite difference actually produces.

Replicates ValueNoise3D_ALU (Engine/Shaders/Private/Random.ush:349) at z=0, which is bilinear
interpolation of iid uniform[-1,1] lattice values through PerlinRamp's quintic weights, and the
level accumulation of MaterialExpressionNoise (Engine/Shaders/Private/Common.ush:1797) which for
Levels=2, LevelScale=2, OutputMin=-1, OutputMax=1 and a negligible FilterWidth reduces exactly to

    N(p) = V(p) + 0.5 * V(2p)

The bump node then forms Dx = (N(p + (STEP,0)) - N(p)) / STEP and returns
normalize(float3(-Dx*S, -Dy*S, 1)), so the tangent of the surface slope it delivers is

    slope = S * |grad|,  grad = (Dx, Dy)

The header claims S IS that tangent, which holds only if |grad| <= 1.
"""

import numpy as np

STEP = 0.25          # HF_BUMP_STEP
LEVELS = 2
LEVEL_SCALE = 2.0
GRID = 4096          # lattice cells per side of the sampled field
SAMPLES = 4_000_000

rng = np.random.default_rng(20260809)

# One lattice per octave, big enough that wrap-around does not correlate the two.
lattice = [rng.uniform(-1.0, 1.0, size=(GRID + 2, GRID + 2)) for _ in range(LEVELS)]


def perlin_ramp(t):
    """PerlinRamp: 6t^5 - 15t^4 + 10t^3, the quintic weight ValueNoise3D_ALU interpolates with."""
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def value_noise(x, y, octave):
    lat = lattice[octave]
    ix = np.floor(x).astype(np.int64) % GRID
    iy = np.floor(y).astype(np.int64) % GRID
    fx = perlin_ramp(x - np.floor(x))
    fy = perlin_ramp(y - np.floor(y))

    c00 = lat[ix, iy]
    c10 = lat[ix + 1, iy]
    c01 = lat[ix, iy + 1]
    c11 = lat[ix + 1, iy + 1]

    return (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy


def fbm(x, y):
    out = np.zeros_like(x)
    scale = 1.0
    freq = 1.0
    for octave in range(LEVELS):
        out += value_noise(x * freq, y * freq, octave) * scale
        freq *= LEVEL_SCALE
        scale /= LEVEL_SCALE
    return out


px = rng.uniform(0.0, GRID, size=SAMPLES)
py = rng.uniform(0.0, GRID, size=SAMPLES)

n_c = fbm(px, py)
n_x = fbm(px + STEP, py)
n_y = fbm(px, py + STEP)

dx = (n_x - n_c) / STEP
dy = (n_y - n_c) / STEP
mag = np.hypot(dx, dy)

print("field N(p) = V(p) + 0.5*V(2p),  step = {}".format(STEP))
print("  N range            {:+.3f} .. {:+.3f}   (declared -1..1)".format(n_c.min(), n_c.max()))
print("  |grad| mean        {:.4f}".format(mag.mean()))
print("  |grad| rms         {:.4f}".format(np.sqrt((mag ** 2).mean())))
print("  |grad| median      {:.4f}".format(np.median(mag)))
print("  |grad| p90         {:.4f}".format(np.percentile(mag, 90)))
print("  |grad| p99         {:.4f}".format(np.percentile(mag, 99)))
print("  |grad| p99.9       {:.4f}".format(np.percentile(mag, 99.9)))
print("  |grad| max         {:.4f}".format(mag.max()))
print()
print("So DetailBumpStrength S delivers a slope of S * |grad|.")
print("  rms slope factor   {:.3f}x".format(np.sqrt((mag ** 2).mean())))
print("  p99 slope factor   {:.3f}x".format(np.percentile(mag, 99)))
print()
for s, label in [(0.045, "Fabric"), (0.022, "WallPaint"), (0.015, "DoorLeaf"),
                 (0.010, "CounterStone"), (0.008, "MetalHardware"), (0.004, "CeilingSoffit"),
                 (0.0015, "WindowFrame")]:
    rms = np.sqrt((mag ** 2).mean()) * s
    p99 = np.percentile(mag, 99) * s
    print("  {:14s} S={:.4f}  rms slope {:5.2f} deg   p99 slope {:5.2f} deg"
          .format(label, s, np.degrees(np.arctan(rms)), np.degrees(np.arctan(p99))))
