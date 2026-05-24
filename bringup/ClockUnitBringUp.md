# ClockUnit Bringup Guide

This document describes the global clock module implemented in `sim/`.
Upload it at the start of any new chat that will work on this simulator.

---

## System Overview

The simulator models two hardware units running at different clock frequencies:

| Unit | Frequency | Period |
|------|-----------|--------|
| SNN  | 20 MHz    | 50 ns  |
| MCBA | 250 MHz   | 4 ns   |

A **GlobalClock** acts as the central timekeeper. It drives all units via an
event-driven scheduler — it does not step nanosecond-by-nanosecond; it jumps
directly to the next scheduled tick. Additional units can be registered at any
frequency, including non-integer-ns periods (e.g. 30 MHz = 100/3 ns).

---

## The Recurring Decimal Problem

Clock periods that are not integer nanoseconds (e.g. 30 MHz → 33.333... ns)
cannot be stored as floats without accumulating drift. The solution is
**rational time representation**: every period is stored as an exact fraction
`{numerator, denominator}` using the `Ratio` type. No floating-point is used
anywhere in the scheduling logic.

The scheduler converts all periods to a common integer space called
**virtual ticks** (vt). The resolution `vt_per_ns` is the LCM of all period
denominators, computed once at `finalize()`.

```
Example with SNN (20 MHz), MCBA (250 MHz), and a 30 MHz unit:

  Periods as fractions of ns:  50/1,  4/1,  100/3
  LCM of denominators: 3     ->  1 vt = 1/3 ns

  SNN  period in vt = 50   × 3 = 150
  MCBA period in vt =  4   × 3 =  12
  30M  period in vt = 100       = 100
```

All scheduling arithmetic is then pure `int64_t` — no fractions, no floats.

---

## Simultaneous Ticks — Two-Phase Model

When two or more units share the same `next_tick_vt`, they fire in **two
strict phases** that mirror synchronous hardware behaviour:

1. **SAMPLE** — all coincident units read their inputs from shared state.
   No unit has written yet. Every unit sees a consistent world snapshot
   from the end of the previous cycle.

2. **COMMIT** — all coincident units write their outputs to shared state.
   This happens only after every SAMPLE handler has completed.

This means a unit can never observe another unit's output from the *same*
cycle — only from the previous one. Handlers are registered per phase:

```cpp
domain->add_handler(TickPhase::SAMPLE, [](const Ratio& t_ns){ /* read  */ });
domain->add_handler(TickPhase::COMMIT, [](const Ratio& t_ns){ /* write */ });
```

---

## File Structure

```
sim/
├── ratio.hpp          Ratio struct — exact rational arithmetic, GCD/LCM utils.
├── timebase.hpp       TimeBase struct — virtual tick resolution, computed at
│                      finalize() from all registered periods.
├── clock_domain.hpp   ClockDomain class — one clocked unit. Holds period (as
│                      Ratio and as vt integer), SAMPLE/COMMIT handler lists,
│                      cycle count, and next_tick_vt.
├── global_clock.hpp   GlobalClock class — central scheduler. Min-heap of
│                      (next_vt, order, domain*) entries. Owns all domains.
└── main.cpp           Top-level entry point. SNN and MCBA handlers are
                       currently placeholders.

test/
└── test_clock.cpp     Self-contained test suite (no external framework).
                       25 tests covering Ratio, TimeBase, ClockDomain,
                       GlobalClock scheduling, simultaneity, and edge cases.
```

All implementation files except `main.cpp` are **header-only**.
Build requirement: **C++17** (`std::gcd`, `std::lcm` from `<numeric>`).

---

## Usage Pattern

```cpp
#include "global_clock.hpp"

GlobalClock clock;

// 1. Register units (before finalize)
ClockDomain* snn  = clock.register_unit_mhz("SNN",   20);
ClockDomain* mcba = clock.register_unit_mhz("MCBA", 250);

// For non-integer MHz, use register_unit() with an explicit Ratio period:
//   ClockDomain* d = clock.register_unit("30M", Ratio(100, 3));  // 100/3 ns

// 2. Attach handlers
snn->add_handler(TickPhase::SAMPLE, [](const Ratio& t){ /* read inputs  */ });
snn->add_handler(TickPhase::COMMIT, [](const Ratio& t){ /* write outputs */ });

mcba->add_handler(TickPhase::SAMPLE, [](const Ratio& t){ /* read inputs  */ });
mcba->add_handler(TickPhase::COMMIT, [](const Ratio& t){ /* write outputs */ });

// 3. Finalize — computes TimeBase, locks registration
clock.finalize();

// 4. Run
clock.run(1000);   // simulate 1000 ns

// 5. Query results
snn->cycle_count();    // number of rising edges delivered
mcba->cycle_count();
clock.current_time_ns_double();
```

`run(N)` fires all ticks at timestamps `<= N` ns. The first tick always fires
at `t = 0`. A single `step(stop_vt)` call processes exactly one event batch
(all domains coincident at the next scheduled virtual tick) and returns `false`
when no further events exist within the stop time.

---

## Key Types

### `Ratio` (`sim/ratio.hpp`)
Exact rational number. Always reduced. Denominator always positive.

```cpp
Ratio(int64_t num, int64_t den)
Ratio::from_freq_mhz(int64_t freq_num, int64_t freq_den = 1)

// Arithmetic: +, -, *, /  (all return reduced Ratio)
// Comparison: ==, !=, <, <=, >, >=
double to_double()     // for display only — never use for scheduling
int64_t to_integer()   // throws if not an exact integer
```

### `TimeBase` (`sim/timebase.hpp`)
Computed by `compute_timebase(vector<Ratio> periods_ns)`.

```cpp
int64_t vt_per_ns         // virtual ticks per nanosecond
int64_t ns_to_vt(ns)      // convert ns to virtual ticks (integer)
int64_t period_to_vt(Ratio period_ns)   // exact period in virtual ticks
Ratio   vt_to_ns_ratio(vt)              // exact inverse
double  vt_to_ns_double(vt)             // for display only
```

### `ClockDomain` (`sim/clock_domain.hpp`)
One clocked unit. Obtained from `GlobalClock::register_unit*()`.

```cpp
void     add_handler(TickPhase, Handler)  // before finalize() only
uint64_t cycle_count()                    // incremented at COMMIT
int64_t  period_vt()                      // period in virtual ticks
int64_t  next_tick_vt()                   // next scheduled vt
```

### `GlobalClock` (`sim/global_clock.hpp`)
```cpp
ClockDomain* register_unit(name, Ratio period_ns)
ClockDomain* register_unit_mhz(name, int64_t freq_mhz)
ClockDomain* register_unit_mhz_ratio(name, int64_t num, int64_t den)
void         finalize()
void         run(int64_t stop_ns)
bool         step(int64_t stop_vt)     // one event batch; returns false when done
const TimeBase& timebase()
Ratio        current_time_ns()
double       current_time_ns_double()
```

---

## Compiler Requirement — STRICT RULE

**Target compiler: GCC 6.3.0 (MinGW.org GCC-6.3.0-1), C++14.**

This is a hard constraint. Every file in this project must compile cleanly with:

```
g++ -std=c++14 -O2 -Wall -Wextra
```

Do NOT use any of the following — they are unavailable or incomplete in GCC 6.3.0:

| Feature | Reason banned |
|---------|--------------|
| `std::gcd`, `std::lcm` | Added to `<numeric>` in C++17 only |
| `std::optional`, `std::variant`, `std::string_view` | C++17 only |
| `if constexpr` | C++17 only |
| Structured bindings (`auto [a, b] = ...`) | C++17 only |
| `-std=c++17` flag | Incomplete support in GCC 6.3.0 |

GCD and LCM are implemented manually as `gcd64()` and `lcm64()` in `ratio.hpp`.
All new code must use these instead of any standard library equivalent.

---

## Build and Test

```bash
# From project root
g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o test/test_clock test/test_clock.cpp
./test/test_clock
# Expected: 25 passed, 0 failed
```

```bash
# Build the main simulation binary
g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o sim/sim sim/main.cpp
```

---

## What Is Not Yet Implemented

- **SNN unit** — handlers in `main.cpp` are empty lambdas (`// TODO`).
- **MCBA unit** — same.
- **Shared state bus** — no inter-unit communication mechanism yet.
  The two-phase model is in place; the shared data structures it protects
  are the responsibility of the unit implementations.
- **CMake / build system** — currently built by hand with a single `g++` call.

---

## Design Decisions Already Made

| Decision | Choice | Reason |
|----------|--------|--------|
| Time representation | Exact `Ratio` fractions | Eliminates float drift for non-integer periods |
| Scheduling | Event-driven min-heap | O(D log D) per batch, not O(duration/resolution) |
| Simultaneity model | Two-phase SAMPLE/COMMIT | Mirrors synchronous hardware; no unit sees same-cycle outputs |
| Virtual tick resolution | LCM of all period denominators | Smallest integer space where every period is exact |
| First tick | Always at `t = 0` | Consistent with hardware reset / power-on behaviour |
| Handler registration cutoff | Before `finalize()` | Hard error if violated — prevents silent misconfiguration |
| Cycle count increment | On COMMIT only | Matches rising-edge semantics in synchronous design |