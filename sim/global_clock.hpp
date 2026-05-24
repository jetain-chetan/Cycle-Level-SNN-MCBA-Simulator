#pragma once

#include "ratio.hpp"
#include "timebase.hpp"
#include "clock_domain.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GlobalClock — central multi-rate simulation scheduler.
//
// Lifecycle
// ---------
//   1. Construct GlobalClock.
//   2. Register all units via register_unit().
//      Attach SAMPLE and COMMIT handlers to each returned ClockDomain*.
//   3. Call finalize() — computes TimeBase, locks registration.
//   4. Call run(stop_ns) to simulate, or step() to advance one event at a time.
//
// Simultaneous ticks
// ------------------
// When two or more domains share the same next_tick_vt, GlobalClock fires them
// in two strict phases:
//
//   Phase 1 — SAMPLE: every coincident domain's SAMPLE handlers run.
//             No domain has yet written its outputs. All see the same
//             consistent world state from the end of the previous cycle.
//
//   Phase 2 — COMMIT: every coincident domain's COMMIT handlers run.
//             Units may now write outputs to shared state.
//
// Between phases, no domain has advanced its next_tick_vt, so the heap
// remains stable during the two-phase sequence.
//
// Event-driven efficiency
// -----------------------
// The scheduler jumps directly to the next scheduled virtual tick rather
// than iterating tick-by-tick. Cost is O(D * log D) per fired event batch,
// where D is the number of registered domains — independent of the ratio
// between the simulation duration and the virtual tick resolution.
// ---------------------------------------------------------------------------

class GlobalClock {
public:
    GlobalClock() : finalized_(false), current_vt_(0) {}

    // ------------------------------------------------------------------
    // Registration phase (before finalize())
    // ------------------------------------------------------------------

    // Register a unit by period in ns (as a Ratio).
    // Returns a raw pointer; GlobalClock owns the ClockDomain.
    ClockDomain* register_unit(const std::string& name, Ratio period_ns) {
        if (finalized_)
            throw std::logic_error(
                "GlobalClock: cannot register units after finalize()");
        domains_.push_back(std::make_unique<ClockDomain>(name, std::move(period_ns)));
        return domains_.back().get();
    }

    // Convenience: register by frequency in MHz (integer MHz).
    ClockDomain* register_unit_mhz(const std::string& name, int64_t freq_mhz) {
        return register_unit(name, Ratio::from_freq_mhz(freq_mhz));
    }

    // Convenience: register by frequency as a rational MHz (e.g. 33.333... MHz
    // expressed as freq_num/freq_den, so 100/3 MHz).
    ClockDomain* register_unit_mhz_ratio(const std::string& name,
                                          int64_t freq_num, int64_t freq_den = 1) {
        return register_unit(name, Ratio::from_freq_mhz(freq_num, freq_den));
    }

    // ------------------------------------------------------------------
    // Finalize — must be called once after all units are registered.
    // ------------------------------------------------------------------

    void finalize() {
        if (finalized_)
            throw std::logic_error("GlobalClock::finalize() called more than once");
        if (domains_.empty())
            throw std::logic_error("GlobalClock::finalize(): no domains registered");

        // Collect all periods and compute virtual tick resolution
        std::vector<Ratio> periods;
        periods.reserve(domains_.size());
        for (auto& d : domains_)
            periods.push_back(d->period_ns());

        timebase_ = compute_timebase(periods);

        // Finalize each domain (converts its period to virtual ticks)
        for (auto& d : domains_)
            d->finalize(timebase_);

        // Seed the priority heap — all domains start at vt=0
        // Heap entry: (next_tick_vt, insertion_order, domain_ptr)
        // insertion_order gives a stable secondary sort so behaviour is
        // deterministic when multiple domains share the same next_tick_vt.
        for (size_t i = 0; i < domains_.size(); ++i)
            heap_.push({ 0, static_cast<int64_t>(i), domains_[i].get() });

        finalized_ = true;
        current_vt_ = 0;
    }

    // ------------------------------------------------------------------
    // Simulation
    // ------------------------------------------------------------------

    // Advance to the next event batch (all domains at the same virtual tick).
    // Returns false if no events remain at or before stop_vt.
    bool step(int64_t stop_vt) {
        if (!finalized_)
            throw std::logic_error("GlobalClock::step() called before finalize()");
        if (heap_.empty()) return false;

        int64_t fire_vt = heap_.top().next_vt;
        if (fire_vt > stop_vt) return false;

        current_vt_ = fire_vt;
        Ratio time_ns = timebase_.vt_to_ns_ratio(current_vt_);

        // Collect every domain whose next tick is exactly fire_vt
        std::vector<ClockDomain*> batch;
        while (!heap_.empty() && heap_.top().next_vt == fire_vt) {
            batch.push_back(heap_.top().domain);
            heap_.pop();
        }

        // --- Phase 1: SAMPLE all domains in the batch ---
        for (ClockDomain* d : batch)
            d->fire(TickPhase::SAMPLE, time_ns);

        // --- Phase 2: COMMIT all domains in the batch ---
        for (ClockDomain* d : batch)
            d->fire(TickPhase::COMMIT, time_ns);

        // Re-queue each domain at its next scheduled tick
        int64_t order = static_cast<int64_t>(next_order_++);
        for (ClockDomain* d : batch) {
            d->advance();
            heap_.push({ d->next_tick_vt(), order++, d });
        }

        return true;
    }

    // Run until simulation time reaches stop_ns (inclusive of the last tick
    // that falls exactly on stop_ns).
    void run(int64_t stop_ns) {
        if (!finalized_)
            throw std::logic_error("GlobalClock::run() called before finalize()");
        int64_t stop_vt = timebase_.ns_to_vt(stop_ns);
        while (step(stop_vt)) {}
    }

    // ------------------------------------------------------------------
    // Accessors
    // ------------------------------------------------------------------

    const TimeBase& timebase()   const { return timebase_; }
    int64_t         current_vt() const { return current_vt_; }

    // Current simulation time in ns (exact rational)
    Ratio current_time_ns() const {
        return timebase_.vt_to_ns_ratio(current_vt_);
    }

    // Current simulation time in ns (double, for display)
    double current_time_ns_double() const {
        return timebase_.vt_to_ns_double(current_vt_);
    }

    // Number of registered domains
    size_t domain_count() const { return domains_.size(); }

    // Access domain by index (registration order)
    ClockDomain* domain(size_t i) const { return domains_.at(i).get(); }

    std::string to_string() const {
        std::string s = "GlobalClock {\n  " + timebase_.to_string() + "\n";
        for (auto& d : domains_)
            s += "  " + d->to_string() + "\n";
        return s + "}";
    }

private:
    // ------------------------------------------------------------------
    // Min-heap entry
    // ------------------------------------------------------------------
    struct HeapEntry {
        int64_t      next_vt;    // next scheduled virtual tick
        int64_t      order;      // tie-break for deterministic ordering
        ClockDomain* domain;

        // Min-heap: smallest next_vt has highest priority
        bool operator>(const HeapEntry& o) const {
            if (next_vt != o.next_vt) return next_vt > o.next_vt;
            return order > o.order;          // stable secondary sort
        }
    };

    using MinHeap = std::priority_queue<HeapEntry,
                                        std::vector<HeapEntry>,
                                        std::greater<HeapEntry>>;

    std::vector<std::unique_ptr<ClockDomain>> domains_;
    TimeBase  timebase_;
    MinHeap   heap_;
    bool      finalized_;
    int64_t   current_vt_;
    size_t    next_order_ = 0;
};