# SNN Layer BringUp Guide

Upload this document alongside `LIFNeuronBringUp.md` and
`ClockUnitBringUp.md` at the start of any chat that will work on the
SNN layer or anything that depends on it.

If the chat will work on the test suite or plotting infrastructure, also
upload `SnnLayerTestBringUp.md`.

---

## What This Module Is

`SnnLayer` is a bank of N `LIFNeuron` instances driven by a common
shaped-clock tick supplied by the caller. It is one abstraction level
above `LIFNeuron` and one level below `SnnUnit` (not yet implemented).

Key properties:
- **Heterogeneous parameters.** Each neuron carries its own `NeuronParams`,
  loaded from a CSV file at construction time.
- **Pure state machine.** No clock, no `GlobalClock` dependency, no `Ratio`.
  The caller decides when `tick()` fires, exactly as `LIFNeuron` does.
- **Allocation-free hot path.** All vectors are sized at construction;
  `tick()` performs no heap allocation.
- **Layer ID.** An 8-bit identifier recording the layer's position in the
  ring-counter round-robin that will live in `SnnUnit`.

---

## File Locations

```
sim/
└── units/
    └── SNN/
        ├── lif_neuron.hpp         LIF neuron state machine (see LIFNeuronBringUp.md)
        ├── snn_layer_loader.hpp   CSV parameter file parser (free function)
        └── snn_layer.hpp          SnnLayer class

test/
├── test_snn_layer.cpp             C++ test runner (15 tests, 3296 assertions)
├── plot_snn_layer_tests.py        Python plotting script (matplotlib/pandas)
└── params/
    ├── uniform_4.csv              4 identical neurons (baseline tests)
    ├── hetero_4.csv               4 neurons with clearly different params
    ├── leak_sweep_4.csv           4 neurons varying only alpha_leak (no refrac)
    ├── single_1.csv               1 neuron (degenerate size test)
    └── large_16.csv               16 neurons with graded parameter sweep
```

All implementation files are **header-only**.
Dependency chain:

```
snn_layer.hpp  ->  snn_layer_loader.hpp  ->  lif_neuron.hpp  ->  q88.hpp  ->  <stdint.h>
                                         ->  <cstdio>, <cstring>, <cstdlib>, <vector>
```

---

## CSV Parameter File Format

Each neuron in a layer has its own row. Row count determines layer width.
All voltage/multiplier columns are doubles; `refrac_period` is an integer.

```csv
vth,vreset,alpha_leak,alpha_th,delta_th,refrac_period
1.0,0.0,0.9492,0.98,1.25,8
1.0,0.0,0.8000,0.95,1.50,4
1.0,0.0,0.6000,0.90,1.75,2
1.0,0.0,0.9800,0.99,1.10,12
```

Values are converted to Q8.8 via `q88_from_double()` at load time.
The loader (`snn_layer_loader.hpp`) is a single free function:

```cpp
bool load_layer_params(const char* filepath,
                        std::vector<NeuronParams>& out);
// Returns true on success (at least one neuron loaded).
// Returns false on file-open failure, header mismatch, or parse error.
// Errors are written to stderr.
```

---

## `SnnLayer` API

### Constructors

```cpp
// Programmatic — for tests and hand-built layers
SnnLayer(uint8_t layer_id, const std::vector<NeuronParams>& param_vec);

// From CSV file — for production use
SnnLayer(uint8_t layer_id, const char* filepath);
// On load failure, size() == 0. Always check size() > 0 after construction.
```

### Core operations

```cpp
void reset();
// Power-on reset for every neuron. Clears spike vector.
// Models rst_n across the whole layer.

const std::vector<uint8_t>& tick(const q88_t* syn_inputs, uint16_t num_inputs);
// Advance all neurons by one timestep.
// syn_inputs[i] is the Q8.8 synaptic input for neuron i.
// num_inputs must equal size(). A mismatch prints a stderr warning and
// leaves state unchanged (no crash, no partial update).
// Returns a const reference to the spike vector (valid until next tick/reset).
// Allocation-free on the hot path.
```

### Accessors

```cpp
uint8_t  layer_id() const;
uint16_t size()     const;

const std::vector<uint8_t>& spike_out() const;   // last tick result (0 or 1 per neuron)

// Per-neuron state inspection — for logging and testing only
q88_t   vmembrane     (uint16_t i) const;
q88_t   vthreshold    (uint16_t i) const;
uint8_t refractory_ctr(uint16_t i) const;

const NeuronParams& params(uint16_t i) const;    // full param set for neuron i
```

---

## Typical Usage

```cpp
#include "sim/units/SNN/snn_layer.hpp"

// --- At setup (before finalize / before first tick) ---

// From CSV (recommended for production):
SnnLayer layer(0, "sim/units/SNN/params/layer0.csv");
if (layer.size() == 0) { /* handle load failure */ }

// Programmatic (tests / hand-crafted scenarios):
std::vector<NeuronParams> pv;
for (int i = 0; i < 8; ++i) {
    NeuronParams p;
    p.vth          = q88_from_double(1.0);
    p.vreset       = q88_from_int(0);
    p.alpha_leak   = q88_from_double(0.9492 - i * 0.02);  // varied per neuron
    p.alpha_th     = q88_from_double(0.98);
    p.delta_th     = q88_from_double(1.25);
    p.refrac_period = (uint8_t)(4 + i);
    pv.push_back(p);
}
SnnLayer layer(0, pv);

// --- Inside the SNN COMMIT handler (shaped-clock trigger) ---
// mcba_output is a Q8.8 buffer of size layer.size(), from the MCBA output bus.
const std::vector<uint8_t>& spikes = layer.tick(mcba_output, layer.size());
// Write spikes to shared state for MCBA SAMPLE on the next SNN tick.
for (uint16_t i = 0; i < layer.size(); ++i) {
    spike_bus[layer.layer_id()][i] = spikes[i];
}
```

---

## Build and Test

```bash
# From project root
g++ -std=c++14 -O2 -Wall -Wextra -I . \
    -o test/test_snn_layer test/test_snn_layer.cpp
./test/test_snn_layer
# Expected: 3296 passed, 0 failed
```

```bash
# Single test in CSV mode:
./test/test_snn_layer --csv t14_hetero_spike_patterns
```

---

## What Is Not Yet Implemented

| Item | Notes |
|------|-------|
| `SnnUnit` | `ClockDomain` wrapper; registers with `GlobalClock`; owns layers; implements the shaped trigger (ring counter, round-robin scheduling) |
| Shared state bus | MCBA output → layer `syn_inputs`; layer `spike_out` → MCBA input |
| Weight matrix | The MCBA computes the weighted sum; the weight matrix is MCBA's responsibility |
| Multi-layer connectivity | `SnnUnit` will manage the ring-counter index and route spikes between layers via the MCBA |

---

## Design Decisions

| Decision | Choice | Reason |
|----------|--------|--------|
| Heterogeneous params | One `NeuronParams` per neuron | Allows biologically realistic diversity; loaded from CSV at construction |
| CSV for parameters | Human-readable doubles → Q8.8 at load time | Editable in spreadsheets, diffable in git, directly usable by existing Python tooling |
| `uint8_t` spike vector | Not `std::vector<bool>` | Avoids bitfield specialisation; elements usable as `q88_t`-compatible values downstream |
| Size-mismatch is a no-op | Prints stderr + returns without updating | Prevents partial updates and silent data corruption; caller must fix the mismatch |
| `layer_id` is `uint8_t` | Sufficient for up to 255 layers | Matches ring-counter intent; saves space in large layer arrays |
| Loader is a free function | `snn_layer_loader.hpp` separate from `snn_layer.hpp` | Isolates file-I/O dependency; easier to mock or replace in unit tests |
| No `ClockDomain` on `SnnLayer` | Pure state machine | Layers have no independent clock; SnnUnit handlers are the trigger |
| Compiler | GCC 6.3.0, `-std=c++14` | Hard project constraint; no C++17 features used anywhere |

---

## Compiler Requirement

Same hard constraint as the rest of the project:

**GCC 6.3.0, `-std=c++14`**

```bash
g++ -std=c++14 -O2 -Wall -Wextra -I. -o test/test_snn_layer test/test_snn_layer.cpp
```

Banned features: `std::gcd`, `std::lcm`, `std::optional`, `std::variant`,
`std::string_view`, `if constexpr`, structured bindings. See
`ClockUnitBringUp.md` for the full list.