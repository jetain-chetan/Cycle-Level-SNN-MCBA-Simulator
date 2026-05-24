# LIF Neuron BringUp Guide

Upload this document (alongside `ClockUnitBringUp.md`) at the start of any
chat that will work on the neuron model or anything that depends on it.

If the chat will work on the test suite or plotting infrastructure, also
upload `LIFNeuronTestBringUp.md`.

---

## Test Suite

A full test suite and Python plotting infrastructure exist. See
`LIFNeuronTestBringUp.md` for details. Quick reference:

```
test/
├── test_lif_neuron.cpp   C++ test runner (12 tests, 1489 assertions)
└── plot_lif_tests.py     Python plotting script (matplotlib/pandas)
```

```bash
# Build and run all assertions (from project root):
g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o test/test_lif_neuron test/test_lif_neuron.cpp
./test/test_lif_neuron
# Expected: 1489 passed, 0 failed
```

---


If the chat will work on the test suite or plotting infrastructure, also
upload `LIFNeuronTestBringUp.md`.

---

## Test Suite

A full test suite and Python plotting infrastructure exist. See
`LIFNeuronTestBringUp.md` for build instructions, test IDs, and
plotting usage. Summary:

```
test/
251C25002500 test_lif_neuron.cpp   C++ test runner (12 tests, 1489 assertions)
251425002500 plot_lif_tests.py     Python plotting script (matplotlib/pandas)
```

Quick run:
```bash
g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o test/test_lif_neuron test/test_lif_neuron.cpp
./test/test_lif_neuron
# Expected: 1489 passed, 0 failed
```

---

ORIGINAL_PLACEHOLDER
chat that will work on the neuron model or anything that depends on it.

---

## What This Module Is

A single **Leaky Integrate-and-Fire (LIF) neuron** implemented as a pure C++
state machine. It has no clock, no scheduler dependency, and no knowledge of
simulated time. The caller decides when to call `tick()`.

It is a sub-unit of the SNN unit. The SNN unit's handlers (registered with
`GlobalClock`) are responsible for triggering `tick()` at the correct moments.

---

## File Locations

```
sim/
├── q88.hpp                      Q8.8 fixed-point helper (shared with MCBA)
└── units/
    └── SNN/
        └── lif_neuron.hpp       LIF neuron state machine
```

Both files are **header-only**. The only dependency chain is:

```
lif_neuron.hpp  ->  q88.hpp  ->  <stdint.h>
```

No GlobalClock, no Ratio, no standard library containers.

---

## Fixed-Point Format: Q8.8

All voltages and multiplier parameters use **Q8.8 signed fixed-point**:

| Property | Value |
|----------|-------|
| Storage type | `int16_t` (typedef'd as `q88_t`) |
| Total bits | 16 |
| Integer bits | 8 (signed, so −128 to +127) |
| Fractional bits | 8 |
| Scale factor | 2⁸ = **256** (1.0 is represented as 256) |
| Resolution | 1/256 ≈ 0.00390625 |
| Range | −128.0 to +127.99609375 |

### `q88.hpp` API

```cpp
typedef int16_t q88_t;

// Setup/display helpers (never call on hot simulation path)
q88_t  q88_from_double(double v);      // e.g. q88_from_double(0.95) == 243
q88_t  q88_from_int(int v);            // e.g. q88_from_int(1)       == 256
double q88_to_double(q88_t v);         // for printf / logging only

// Core arithmetic (safe to call on simulation path)
q88_t  q88_mul(q88_t a, q88_t b);     // Q8.8 × Q8.8 → Q8.8
```

### `q88_mul` implementation note

The multiply widens to `int32_t` (Q16.16 product), then reinterprets as
`uint32_t` for a **logical** right-shift by 8 — giving a defined result under
C++14. The lower 16 bits are then reinterpreted back to `int16_t` via a union
(the C++14-portable type-pun idiom). This avoids implementation-defined
signed right-shift behaviour entirely.

**Precondition**: the product fed into `q88_mul` must be non-negative before
the shift. This is guaranteed by the neuron's clamp logic (see below); it is
not enforced inside `q88_mul` itself.

---

## Neuron Model: `lif_neuron.hpp`

### `NeuronParams` struct

All parameters must be set explicitly — there are no defaults.

```cpp
struct NeuronParams {
    q88_t   vth;            // Base threshold. Also the floor for dynamic threshold.
    q88_t   vreset;         // Membrane reset/rest potential. Must be >= 0.
    q88_t   alpha_leak;     // Leak multiplier per timestep  (fraction, e.g. 0.95 → 243)
    q88_t   alpha_th;       // Threshold decay per timestep  (fraction, e.g. 0.98 → 251)
    q88_t   delta_th;       // Threshold boost on spike      (can be > 1.0, e.g. 1.25 → 320)
    uint8_t refrac_period;  // Refractory window in timesteps (max 255)
};
```

Reference parameter values (from Verilog `testRefrac.v`, re-scaled to Q8.8):

| Parameter | Meaning | Q6.7 value | **Q8.8 value** |
|-----------|---------|-----------|--------------|
| `vth` | Threshold | 128 | **256** |
| `vreset` | Rest potential | 0 | **0** |
| `alpha_leak` | Membrane leak | 122 | **243** |
| `alpha_th` | Threshold decay | 125 | **251** |
| `delta_th` | Threshold boost | 160 | **320** |
| `refrac_period` | Refractory steps | 8 | **8** (unchanged) |

### `LIFNeuron` class

```cpp
// Construction — copies params, calls reset() internally.
explicit LIFNeuron(const NeuronParams& params);

// Reset to power-on state (models rst_n).
void reset();

// Advance by one timestep. Returns true if the neuron fires.
// syn_input: one Q8.8 element from the MCBA result vector.
bool tick(q88_t syn_input);

// Read-only state accessors (for logging/testing only).
bool    spike_out()      const;
q88_t   vmembrane()      const;
q88_t   vthreshold()     const;
uint8_t refractory_ctr() const;

// Access the full parameter set.
const NeuronParams& params() const;
```

---

## tick() — State Update Logic

Each call to `tick()` executes the following steps in order. This exactly
mirrors the Verilog `always @(posedge clk)` block.

### Step 1 — Pre-multiply clamp

`vm_sum = vmembrane + syn_input`. If this is below `vreset`, it is clamped
to `vreset` before the multiply. This satisfies `q88_mul`'s non-negative
precondition and models the hardware floor on membrane potential.

### Step 2 — Compute next-state candidates

```
vm_next = q88_mul(vm_sum,      alpha_leak)   // leaky integration
vt_next = q88_mul(vthreshold,  alpha_th)     // threshold decay
vb_next = q88_mul(vthreshold,  delta_th)     // threshold boost (post-spike)
```

### Step 3 — Post-multiply clamps

```
vm_next = max(vm_next, vreset)   // membrane floor
vt_next = max(vt_next, vth)      // threshold floor (never below baseline)
```

### Step 4 — Branch (priority order: refractory > spike > integrate)

```
if (refractory_ctr > 0):
    refractory_ctr -= 1
    spike_out  = false
    vmembrane  = vreset        // held at rest during refractory
    vthreshold = vt_next       // threshold still decays in refractory

else if (vm_next >= vthreshold):    // NOTE: compares against CURRENT vthreshold
    spike_out  = true
    vmembrane  = vreset        // reset after spike
    vthreshold = vb_next       // boost threshold
    refractory_ctr = refrac_period

else:
    spike_out  = false
    vmembrane  = vm_next
    vthreshold = vt_next
    refractory_ctr = 0
```

**Critical**: the spike comparison uses `vthreshold_` (the register value
from the previous timestep), not `vt_next`. This is deliberate and matches
the Verilog register semantics: both Vm and Vth update at the same edge,
so the comparison must use the pre-edge threshold.

---

## Clamp Policy Summary

| Variable | Floor | Ceiling | Reason |
|----------|-------|---------|--------|
| `vm_sum` (pre-multiply) | `vreset` | — | Prevents inhibitory input from making product negative; models hardware floor |
| `vm_next` (post-multiply) | `vreset` | — | Belt-and-suspenders; also handles residual rounding |
| `vt_next` (post-multiply) | `vth` | — | Threshold cannot decay below its baseline |
| `vb_next` | — | — | No clamp; boost can exceed normal operating range |

---

## Typical Usage (from an SNN handler)

```cpp
#include "sim/units/SNN/lif_neuron.hpp"

// --- At setup ---
NeuronParams p;
p.vth          = q88_from_double(1.0);
p.vreset       = q88_from_int(0);
p.alpha_leak   = q88_from_double(0.95);
p.alpha_th     = q88_from_double(0.98);
p.delta_th     = q88_from_double(1.25);
p.refrac_period = 8;

LIFNeuron neuron(p);

// --- Inside the SNN COMMIT handler ---
// syn_input is one Q8.8 element from the MCBA output vector.
q88_t   syn_input = mcba_output_bus[neuron_index];
bool    fired     = neuron.tick(syn_input);
spike_bus[neuron_index] = fired;    // written to shared state for MCBA SAMPLE
```

---

## What Is Not Yet Implemented

| Item | Notes |
|------|-------|
| `SnnLayer` | Container for N neurons + input/output buses |
| `SnnUnit` | `ClockDomain` wrapper; registers with `GlobalClock`; owns layers; implements the shaped trigger (ring counter, round-robin scheduling) |
| Shared state bus | MCBA output → neuron input, neuron spike → MCBA input |

The shaped trigger plan: a **X-bit ring counter** (X = number of layers)
inside the SNN unit's COMMIT handler drives round-robin scheduling of which
layer's spike vector is sent to the MCBA each SNN tick. This is not
implemented at the neuron level.

---

## Compiler Requirement

Same hard constraint as the rest of the project:

**GCC 6.3.0, `-std=c++14`**

```bash
g++ -std=c++14 -O2 -Wall -Wextra -I. -o check check.cpp
```

Banned features: `std::gcd`, `std::lcm`, `std::optional`, `std::variant`,
`std::string_view`, `if constexpr`, structured bindings. See
`ClockUnitBringUp.md` for the full list.

---

## Design Decisions

| Decision | Choice | Reason |
|----------|--------|--------|
| No `ClockDomain` on `LIFNeuron` | Pure state machine | Neurons have no independent clock; SNN handlers are the trigger |
| Q8.8 over Q6.7 | Wider integer range (−128..+127) | Accommodates larger synaptic currents and threshold values |
| Logical right-shift via `uint32_t` | Defined behaviour in C++14 | Signed right-shift is implementation-defined; avoids the issue entirely |
| Pre-multiply clamp to `vreset` | Clamp `vm_sum` before `q88_mul` | Satisfies `q88_mul` precondition; no negative products enter the shift |
| Threshold floor = `vth` | `max(vt_next, vth)` | Prevents adaptive threshold decaying below designed baseline |
| Spike comparison vs current `vthreshold_` | Uses register value, not `vt_next` | Matches Verilog register semantics exactly |
| `uint8_t` for `refractory_ctr` | Sufficient for up to 255 steps | Matches `REFRAC_WIDTH` intent; saves space in large neuron arrays |