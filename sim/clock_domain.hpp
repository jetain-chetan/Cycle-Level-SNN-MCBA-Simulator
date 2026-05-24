#pragma once

#include "ratio.hpp"
#include "timebase.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

// ---------------------------------------------------------------------------
// TickPhase — controls when a handler fires within a two-phase tick.
//
// Two-phase model for simultaneous units:
//
//   PHASE_SAMPLE  (phase 0): all units READ their inputs from shared state.
//                            No unit writes yet. Every unit sees a consistent
//                            snapshot of the world at time T.
//
//   PHASE_COMMIT  (phase 1): all units WRITE their outputs to shared state.
//                            Happens only after every unit has finished sampling.
//
// This ensures that when SNN and MCBA tick at the same virtual time, neither
// sees the other's updated output — they both observe the state from the end
// of the previous cycle, exactly as in synchronous digital hardware.
// ---------------------------------------------------------------------------
enum class TickPhase : int {
    SAMPLE = 0,   // read inputs, compute next state internally
    COMMIT = 1,   // write outputs, update shared state
};

// ---------------------------------------------------------------------------
// ClockDomain — one clocked subsystem in the simulation.
//
// Each domain has:
//   - A rational period (in ns), set at registration
//   - An integer period in virtual ticks, set at finalize()
//   - Separate handler lists for SAMPLE and COMMIT phases
//   - A running cycle count (incremented at COMMIT)
//
// Handlers receive the current simulation time as a Ratio in ns (exact)
// so that unit logic can do time-based decisions without float error.
//
// Usage:
//   auto* snn = clock.register_unit("SNN", Ratio::from_freq_mhz(20));
//   snn->add_handler(TickPhase::SAMPLE, [](const Ratio& t){ ... });
//   snn->add_handler(TickPhase::COMMIT, [](const Ratio& t){ ... });
// ---------------------------------------------------------------------------
class ClockDomain {
public:
    using Handler = std::function<void(const Ratio& time_ns)>;

    ClockDomain(std::string name, Ratio period_ns)
        : name_(std::move(name))
        , period_ns_(std::move(period_ns))
        , period_vt_(0)           // set by finalize()
        , next_tick_vt_(0)        // first tick at t=0
        , cycle_count_(0)
        , finalized_(false)
    {}

    // ------------------------------------------------------------------
    // Configuration (call before GlobalClock::finalize())
    // ------------------------------------------------------------------

    void add_handler(TickPhase phase, Handler h) {
        if (finalized_)
            throw std::logic_error(
                "ClockDomain(" + name_ + "): cannot add handlers after finalize()");
        handlers_[static_cast<int>(phase)].push_back(std::move(h));
    }

    // ------------------------------------------------------------------
    // Called by GlobalClock::finalize() only
    // ------------------------------------------------------------------

    void finalize(const TimeBase& tb) {
        period_vt_  = tb.period_to_vt(period_ns_);
        next_tick_vt_ = 0;   // first event at virtual tick 0
        finalized_ = true;
    }

    // ------------------------------------------------------------------
    // Called by GlobalClock during the simulation loop
    // ------------------------------------------------------------------

    // Fire one phase. GlobalClock calls SAMPLE across all coincident domains
    // first, then COMMIT across all of them.
    void fire(TickPhase phase, const Ratio& time_ns) {
        if (phase == TickPhase::COMMIT)
            ++cycle_count_;
        for (auto& h : handlers_[static_cast<int>(phase)])
            h(time_ns);
    }

    // Advance next_tick_vt to the tick after the one just fired.
    void advance() {
        next_tick_vt_ += period_vt_;
    }

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    const std::string& name()         const { return name_; }
    const Ratio&       period_ns()    const { return period_ns_; }
    int64_t            period_vt()    const { return period_vt_; }
    int64_t            next_tick_vt() const { return next_tick_vt_; }
    uint64_t           cycle_count()  const { return cycle_count_; }
    bool               finalized()    const { return finalized_; }

    std::string to_string() const {
        double freq_mhz = 1000.0 / period_ns_.to_double();
        return "ClockDomain { name=" + name_ +
               ", period=" + period_ns_.to_string() + " ns" +
               ", freq=" + std::to_string(freq_mhz) + " MHz" +
               ", period_vt=" + std::to_string(period_vt_) +
               ", cycles=" + std::to_string(cycle_count_) + " }";
    }

private:
    std::string name_;
    Ratio       period_ns_;
    int64_t     period_vt_;
    int64_t     next_tick_vt_;
    uint64_t    cycle_count_;
    bool        finalized_;

    // Index 0 = SAMPLE handlers, index 1 = COMMIT handlers
    std::vector<Handler> handlers_[2];
};