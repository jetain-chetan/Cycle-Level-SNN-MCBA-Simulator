#pragma once

#include "ratio.hpp"
#include <cstdint>
#include <vector>
#include <stdexcept>

// ---------------------------------------------------------------------------
// TimeBase — computed once at finalize(), shared by all ClockDomains.
//
// Problem: given N units with periods p0, p1, ..., pN-1 (each a Ratio in ns),
// find the smallest integer subdivision T_vt such that every period is an
// exact integer multiple of T_vt.
//
// Solution:
//   Write each period as  pᵢ = (numᵢ / denᵢ) ns.
//   Let D = LCM(den0, den1, ...).           <- common denominator
//   Then pᵢ × D is a whole number for every i.
//   So T_vt = 1/D ns  (one virtual tick = 1/D nanoseconds).
//   Period of unit i in virtual ticks = pᵢ × D = numᵢ × (D / denᵢ).
//
// All scheduling arithmetic is then pure int64_t — no fractions, no floats.
//
// Example:
//   SNN  20  MHz  -> period = 50    ns = 50/1  -> vt period = 150  (D=3)
//   MCBA 250 MHz  -> period =  4    ns = 4/1   -> vt period =  12  (D=3)
//   FOO  30  MHz  -> period = 100/3 ns          -> vt period = 100  (D=3)
// ---------------------------------------------------------------------------

struct TimeBase {
    int64_t vt_per_ns;          // virtual ticks per nanosecond  (= D)
    // inverse: 1 virtual tick = 1/vt_per_ns ns

    // Convert a simulation stop-time in ns to virtual ticks (ceiling).
    int64_t ns_to_vt(int64_t ns) const {
        return ns * vt_per_ns;
    }

    // Convert virtual ticks back to ns as a Ratio (exact).
    Ratio vt_to_ns_ratio(int64_t vt) const {
        return Ratio(vt, vt_per_ns);
    }

    // Convert virtual ticks to ns as a double (for display only).
    double vt_to_ns_double(int64_t vt) const {
        return static_cast<double>(vt) / static_cast<double>(vt_per_ns);
    }

    // Given a period as a Ratio in ns, return its exact integer period in vt.
    int64_t period_to_vt(const Ratio& period_ns) const {
        // period_vt = period_ns * vt_per_ns
        // = (num/den) * vt_per_ns
        // vt_per_ns is constructed so that den divides it exactly.
        int64_t result_num = period_ns.num * vt_per_ns;
        if (result_num % period_ns.den != 0)
            throw std::logic_error(
                "TimeBase::period_to_vt: period " + period_ns.to_string() +
                " ns is not an integer multiple of the virtual tick");
        return result_num / period_ns.den;
    }

    // Human-readable description
    std::string to_string() const {
        return "TimeBase { 1 vt = 1/" + std::to_string(vt_per_ns) +
               " ns,  vt_per_ns = " + std::to_string(vt_per_ns) + " }";
    }
};

// ---------------------------------------------------------------------------
// Compute TimeBase from a list of clock periods (each a Ratio in ns).
// Call this once after all units are registered.
// ---------------------------------------------------------------------------
inline TimeBase compute_timebase(const std::vector<Ratio>& periods_ns) {
    if (periods_ns.empty())
        throw std::invalid_argument("compute_timebase: no periods provided");

    // D = LCM of all denominators
    int64_t D = 1;
    for (const auto& p : periods_ns)
        D = lcm64(D, p.den);

    return TimeBase{ D };
}