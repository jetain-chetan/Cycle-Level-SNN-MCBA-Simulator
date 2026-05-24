# LIF Neuron Test & Infrastructure BringUp Guide

Upload this document alongside `LIFNeuronBringUp.md` and
`ClockUnitBringUp.md` at the start of any chat that will work on the
LIF neuron test suite or plotting infrastructure.

---

## File Locations

```
test/
├── test_lif_neuron.cpp    C++ test runner — all 12 tests, assertion engine,
│                          and CSV emitter. Header-only: no .cpp dependencies.
└── plot_lif_tests.py      Python plotting infrastructure. Drives the C++
                           binary, reads CSV, produces matplotlib figures.

test/plots/                Generated plot files (not committed).
                           Created automatically by plot_lif_tests.py --save.
```

The test suite sits at the same root level as `sim/`. Build and run from
the project root (the directory containing both `sim/` and `test/`).

---

## Build

```bash
# From project root
g++ -std=c++14 -O2 -Wall -Wextra -I sim/ \
    -o test/test_lif_neuron test/test_lif_neuron.cpp
```

Compiler requirement: **GCC 6.3.0, `-std=c++14`** — same hard constraint as
the rest of the project. The test file uses no features beyond C++14.

---

## Running the Test Suite

### All tests (assertion mode)

```bash
./test/test_lif_neuron
# Expected output to stderr:
# 1489 passed, 0 failed (of 1489)
# Exit code 0 on pass, 1 on any failure.
```

Failures print to `stderr` in the form:
```
  FAIL  test/test_lif_neuron.cpp:123  ->  expression_that_failed
```

### Single test — CSV mode

```bash
./test/test_lif_neuron --csv <test_id>
```

CSV is written to `stdout`. Assertions still run (stderr). Redirect:

```bash
./test/test_lif_neuron --csv t01_dynamic_threshold_a > /tmp/t01a.csv
```

### CSV format

```
tick,syn_input,vmembrane,vthreshold,refractory_ctr,spike_out
0,0.500000,0.474609,1.000000,0,0
1,0.500000,0.921875,1.000000,0,0
...
```

All voltage columns are doubles (Q8.8 converted via `q88_to_double`).
`refractory_ctr` is an unsigned integer.
`spike_out` is 0 or 1.

---

## Test Catalogue

Tests 7, 10, and 11 are assertion-only (no `--csv` output).
All others emit CSV when passed to `--csv`.

| ID | Name | CSV? | What it tests |
|----|------|------|---------------|
| `t01_dynamic_threshold_a` | Dynamic Threshold (default) | ✓ | Threshold boost after spike, decay toward vth, floor enforcement |
| `t01_dynamic_threshold_b` | Dynamic Threshold (slow decay) | ✓ | Same with alpha_th=0.995 — very slow recovery |
| `t01_dynamic_threshold_c` | Dynamic Threshold (strong boost) | ✓ | Same with delta_th=1.5, alpha_th=0.92 |
| `t02_rate_encoding_full` | Rate Encoding (full) | ✓ | Spike frequency increases monotonically with constant input |
| `t02_rate_encoding_no_refrac` | Rate Encoding (no refractory) | ✓ | Same with refrac_period=0 |
| `t02_rate_encoding_no_thresh` | Rate Encoding (static threshold) | ✓ | Same with alpha_th=1.0, delta_th=1.0; ISI must be perfectly regular |
| `t03_refractory_period_0` | Refractory (period=0) | ✓ | Consecutive-tick firing allowed |
| `t03_refractory_period_4` | Refractory (period=4) | ✓ | ISI = 5 exactly |
| `t03_refractory_period_8` | Refractory (period=8) | ✓ | ISI = 9 exactly |
| `t03_refractory_period_16` | Refractory (period=16) | ✓ | ISI = 17 exactly |
| `t04_vm_decay_a` | Vm Decay (alpha=0.95) | ✓ | Sub-threshold burst, then decay to vreset |
| `t04_vm_decay_b` | Vm Decay (alpha=0.80) | ✓ | Faster decay |
| `t04_vm_decay_c` | Vm Decay (alpha=0.50) | ✓ | Very fast decay |
| `t05_inhibitory_clamp` | Inhibitory Input | ✓ | Vm never drops below vreset under strongly negative input |
| `t06_threshold_floor` | Threshold Floor | ✓ | Threshold asymptotes to vth and stops; never dips below |
| `t07_reset_correctness` | Reset | ✗ | reset() at mid-integrate / mid-refractory / post-spike; post-reset behaviour identical to fresh neuron |
| `t08_refrac_thresh_interaction` | Refrac/Threshold Interaction | ✓ | Threshold continues decaying during refractory window |
| `t09_min_isi_0` | Min ISI (period=0) | ✓ | ISI histogram: single bin at 1 |
| `t09_min_isi_4` | Min ISI (period=4) | ✓ | ISI histogram: single bin at 5 |
| `t09_min_isi_8` | Min ISI (period=8) | ✓ | ISI histogram: single bin at 9 |
| `t10_q88_unit` | Q8.8 Unit Tests | ✗ | q88_mul exact cases; from_double/from_int round-trips |
| `t11_param_quantisation` | Parameter Quantisation | ✗ | Q8.8 resolution = 1/256; documents alpha=0.999 → 255, not 256 |
| `t12_long_run_stability` | Long-Run Stability | ✓ | 2000 ticks sub-threshold; Vm converges, no rounding drift |

### ISI expected values for T03 and T09

With a suprathreshold constant input and static threshold:

```
ISI = refrac_period + 1
```

One tick fires (spike), then `refrac_period` silent ticks before next fire.
`refrac_period = 0` → ISI = 1 (fires every tick).

### T12 equilibrium calculation

The Q8.8 fixed-point equilibrium for sub-threshold integration:

```
Vm* = (alpha_leak × inp) / (1 - alpha_leak)
```

Using the test parameters (alpha_leak=0.9492, inp=0.1):

```
Vm* ≈ 0.9492 × 0.1 / (1 - 0.9492) ≈ 1.87
```

Well below vth=5.0, confirming no spikes occur.

---

## Assertion Infrastructure

The test runner is self-contained with two macros:

```cpp
CHECK(expr)
// Records pass/fail. On failure prints file:line and the expression.

CHECK_Q88_EQ(got, expected, label)
// Q8.8-aware equality check. On failure prints both the raw integer
// value and the human-readable double (via q88_to_double).
```

Counts are accumulated in three file-scope integers:
`g_tests_run`, `g_tests_passed`, `g_tests_failed`.

Summary is always printed to `stderr` so it does not contaminate CSV output
on `stdout`.

---

## Python Plotting Infrastructure

### Dependencies

```bash
pip install matplotlib pandas numpy
```

### Usage

```bash
# List all available test IDs:
python3 test/plot_lif_tests.py --list

# Plot one test interactively:
python3 test/plot_lif_tests.py t01_dynamic_threshold_a

# Plot one test, save to test/plots/:
python3 test/plot_lif_tests.py t01_dynamic_threshold_a --save

# Plot all tests, save all:
python3 test/plot_lif_tests.py --all --save
```

Run from the **project root** (the directory containing `sim/` and `test/`).

### Standard plot layout

For most tests, each variant is shown in a column with three panels:

```
┌─────────────────────────────────┐
│  Vmembrane (blue) +             │   row 1 — 3× height
│  Vthreshold (orange, dashed)    │
├─────────────────────────────────┤
│  Spike raster (pink verticals)  │   row 2 — 0.8× height
├─────────────────────────────────┤
│  Syn_input (green step)         │   row 3 — 1.2× height
└─────────────────────────────────┘
```

All columns within a group share the same x-axis scale.

### Specialised plots

| Test group | Specialised figure |
|------------|-------------------|
| T09 | ISI histogram: one bar per unique ISI value. Correct = single bar. |
| T12 | Full 2000-tick trace + zoomed 200-tick transient; equilibrium annotated. |

### Generated plot files (--all --save)

```
test/plots/
├── t01_dynamic_threshold.png
├── t02_rate_encoding.png
├── t03_refractory_period.png
├── t04_vmembrane_decay.png
├── t05_inhibitory_input.png
├── t06_threshold_floor.png
├── t08_refrac_threshold_interaction.png
├── t09_min_inter-spike_interval.png   (standard layout)
├── t09_isi_distributions.png          (ISI histogram)
├── t12_long-run_stability.png         (standard layout)
└── t12_stability_detail.png           (zoomed transient)
```

### How the pipeline works

```
plot_lif_tests.py
    │
    ├── subprocess.run(["./test/test_lif_neuron", "--csv", test_id])
    │       stdout → CSV text
    │
    ├── pandas.read_csv(io.StringIO(csv_text))
    │       → DataFrame: tick, syn_input, vmembrane, vthreshold,
    │                    refractory_ctr, spike_out
    │
    └── matplotlib figure
            axes: Vm+Vth / spike raster / syn_input
```

The simulation arithmetic is entirely in C++. Python only handles reading,
layout, and rendering — no re-implementation of neuron logic.

---

## Adding a New Test

### In `test_lif_neuron.cpp`

1. Write a function `void tNN_name(bool emit_csv, const char* sub)`.
2. Inside, use `CHECK(...)` and/or `CHECK_Q88_EQ(...)` for assertions.
3. If CSV: call `csv_header()` once, then `csv_row(tick, inp, neuron)`
   each tick — but only when `emit_csv && sub && strcmp(sub, "tNN_id") == 0`.
4. Call the function from `main()`.

### In `plot_lif_tests.py`

1. Add the new test ID(s) to `TEST_GROUPS` under an appropriate group key.
2. If the test needs a specialised plot layout, add a `make_*_figure()`
   function and call it from `main()` in both `--all` and single-ID paths.

---

## Known Limitations

| Item | Notes |
|------|-------|
| T02 rate sweep | The Python script plots only the middle input level (0.7) as a single-level CSV; a full rate curve requires running the binary once per level and aggregating. Not yet implemented in the plotter. |
| T11 | No CSV output; the Q8.8 resolution boundary behaviour is logged as informational only — no assertion on the half-step rounding direction. |
| `--csv` with assertion-only tests | The binary prints no CSV and exits cleanly; the Python script emits a warning and skips the plot. |