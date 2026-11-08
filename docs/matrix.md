# Andromeda performance matrix

Test source: pluck at 40 %. Budget 1.451 ms per 64-sample block. Baseline (every block off) 0.213 ms.

| scenario | core 0 mean ms | core 0 max ms | mean % | max % | net % | core 1 ms | core 1 % | cut | EFFECT ms | LAYER ms | SPACE ms | over budget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline: every block off | 0.213 | 0.232 | 15 | 16 | 0 | 0.000 | 0 | 0 | 0.000 | 0.000 | 0.000 | 0 |
| SPACE alone (the FDN reverb) | 0.541 | 0.691 | 37 | 48 | 23 | 0.000 | 0 | 0 | 0.000 | 0.000 | 0.348 | 0 |
| SPACE alone + SHIMMER | 0.752 | 0.908 | 52 | 63 | 37 | 0.000 | 0 | 0 | 0.000 | 0.000 | 0.558 | 0 |
| GLASS CRYSTAL | 0.761 | 0.965 | 52 | 67 | 38 | 0.000 | 0 | 0 | 0.212 | 0.000 | 0.351 | 0 |
| GLASS BELL | 0.771 | 1.003 | 53 | 69 | 38 | 0.000 | 0 | 0 | 0.223 | 0.000 | 0.350 | 0 |
| BLOOM GARDEN | 0.686 | 0.929 | 47 | 64 | 33 | 0.000 | 0 | 0 | 0.132 | 0.000 | 0.354 | 0 |
| BLOOM DUSK | 0.686 | 0.900 | 47 | 62 | 33 | 0.000 | 0 | 0 | 0.131 | 0.000 | 0.354 | 0 |
| ACCRETION SOS (playing) | 0.716 | 0.901 | 49 | 62 | 35 | 0.000 | 0 | 0 | 0.148 | 0.000 | 0.359 | 0 |
| ACCRETION SOS (overdubbing) | 0.571 | 0.874 | 39 | 60 | 25 | 0.537 | 37 | 2 | 0.500 | 0.000 | 0.376 | 0 |
| ACCRETION TAPE (playing) | 0.716 | 0.912 | 49 | 63 | 35 | 0.000 | 0 | 0 | 0.148 | 0.000 | 0.359 | 0 |
| ACCRETION DECAY (playing) | 0.716 | 0.907 | 49 | 63 | 35 | 0.000 | 0 | 0 | 0.149 | 0.000 | 0.360 | 0 |
| ACCRETION MIRROR (playing) | 0.715 | 0.899 | 49 | 62 | 35 | 0.000 | 0 | 0 | 0.148 | 0.000 | 0.359 | 0 |
| ACCRETION MIRROR (overdubbing) | 0.571 | 0.920 | 39 | 63 | 25 | 0.537 | 37 | 2 | 0.500 | 0.000 | 0.376 | 0 |
| CHOIR ANGELS | 0.531 | 0.759 | 37 | 52 | 22 | 0.267 | 18 | 2 | 0.231 | 0.000 | 0.355 | 0 |
| CHOIR MOURN | 0.531 | 0.760 | 37 | 52 | 22 | 0.267 | 18 | 2 | 0.230 | 0.000 | 0.355 | 0 |
| FRACTURE CLOCK | 0.614 | 0.801 | 42 | 55 | 28 | 0.000 | 0 | 0 | 0.060 | 0.000 | 0.354 | 0 |
| FRACTURE WATER | 0.623 | 0.852 | 43 | 59 | 28 | 0.000 | 0 | 0 | 0.069 | 0.000 | 0.354 | 0 |
| CONSTELLATION STARDUST | 0.634 | 0.802 | 44 | 55 | 29 | 0.000 | 0 | 0 | 0.069 | 0.000 | 0.360 | 0 |
| CONSTELLATION GRAVITY | 0.638 | 0.804 | 44 | 55 | 29 | 0.000 | 0 | 0 | 0.072 | 0.000 | 0.360 | 0 |
| GLASS CRYSTAL + GLASS layer | 0.527 | 0.746 | 36 | 51 | 22 | 0.494 | 34 | 4 | 0.216 | 0.239 | 0.352 | 0 |
| BLOOM GARDEN + GLASS layer | 0.545 | 0.910 | 38 | 63 | 23 | 0.420 | 29 | 4 | 0.139 | 0.241 | 0.365 | 0 |
| ACCRETION SOS + GLASS layer | 0.550 | 0.821 | 38 | 57 | 23 | 0.434 | 30 | 4 | 0.155 | 0.239 | 0.365 | 0 |
| CHOIR ANGELS + GLASS layer | 0.540 | 0.788 | 37 | 54 | 23 | 0.519 | 36 | 4 | 0.234 | 0.245 | 0.360 | 0 |
| FRACTURE CLOCK + GLASS layer | 0.535 | 0.798 | 37 | 55 | 22 | 0.345 | 24 | 4 | 0.064 | 0.242 | 0.355 | 0 |
| CONSTELLATION STARDUST + GLASS layer | 0.541 | 0.792 | 37 | 55 | 23 | 0.354 | 24 | 4 | 0.072 | 0.238 | 0.361 | 0 |
| CHOIR ANGELS + SHIMMER | 0.745 | 0.938 | 51 | 65 | 37 | 0.267 | 18 | 4 | 0.231 | 0.000 | 0.567 | 0 |
| BLOOM GARDEN + layer + SHIMMER | 0.764 | 1.000 | 53 | 69 | 38 | 0.417 | 29 | 4 | 0.139 | 0.239 | 0.580 | 0 |
| ACCRETION SOS + layer, no SPACE | 0.575 | 0.741 | 40 | 51 | 25 | 0.000 | 0 | 0 | 0.141 | 0.236 | 0.000 | 0 |
| BLOOM GARDEN + layer, no SPACE | 0.560 | 0.752 | 39 | 52 | 24 | 0.000 | 0 | 0 | 0.126 | 0.238 | 0.000 | 0 |

SRAM: 398.0 KB static of 512 KB; .pool region 336.0 KB, 46.7 KB free; effect overlay 147.0 KB; DSP pool peak 72.0 KB of 64.0 KB.
