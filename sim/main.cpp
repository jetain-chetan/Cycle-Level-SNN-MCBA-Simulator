// main.cpp — Top-level simulation entry point.
//
// This file wires together all simulation units and drives the global clock.
// Currently a placeholder: SNN and MCBA units are not yet implemented.
// Their headers will be included here once available.
//
// Build:
//   g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o sim/sim sim/main.cpp
//   (all other files are header-only)

#include "global_clock.hpp"
#include <iostream>

// TODO: include unit headers once implemented
// #include "snn/snn_unit.hpp"
// #include "mcba/mcba_unit.hpp"

int main() {
    // ------------------------------------------------------------------
    // 1. Construct the global clock
    // ------------------------------------------------------------------
    GlobalClock clock;

    // ------------------------------------------------------------------
    // 2. Register simulation units
    //
    //    SNN  runs at  20 MHz  ->  period = 50    ns
    //    MCBA runs at 250 MHz  ->  period =  4    ns
    //
    //    Additional units can be added here. The TimeBase is recomputed
    //    automatically at finalize() to accommodate any new period.
    // ------------------------------------------------------------------
    ClockDomain* snn  = clock.register_unit_mhz("SNN",   20);
    ClockDomain* mcba = clock.register_unit_mhz("MCBA", 250);

    // ------------------------------------------------------------------
    // 3. Attach unit logic
    //
    //    Replace these placeholder lambdas with calls into the real unit
    //    objects once SNN and MCBA are implemented.
    //
    //    SAMPLE: unit reads shared inputs, computes next internal state.
    //    COMMIT: unit writes outputs to shared bus / memory.
    // ------------------------------------------------------------------

    // --- SNN placeholder ---
    snn->add_handler(TickPhase::SAMPLE, [](const Ratio& /*t*/) {
        // TODO: snn_unit.sample(t);
    });
    snn->add_handler(TickPhase::COMMIT, [](const Ratio& /*t*/) {
        // TODO: snn_unit.commit(t);
    });

    // --- MCBA placeholder ---
    mcba->add_handler(TickPhase::SAMPLE, [](const Ratio& /*t*/) {
        // TODO: mcba_unit.sample(t);
    });
    mcba->add_handler(TickPhase::COMMIT, [](const Ratio& /*t*/) {
        // TODO: mcba_unit.commit(t);
    });

    // ------------------------------------------------------------------
    // 4. Finalize — locks registration and computes TimeBase
    // ------------------------------------------------------------------
    clock.finalize();

    std::cout << clock.to_string() << "\n";

    // ------------------------------------------------------------------
    // 5. Run simulation
    //
    //    run(N) simulates N nanoseconds of wall-clock time.
    //    Adjust the duration as required.
    // ------------------------------------------------------------------
    constexpr int64_t SIM_DURATION_NS = 1000;   // 1 microsecond
    clock.run(SIM_DURATION_NS);

    // ------------------------------------------------------------------
    // 6. Print end-of-run stats
    // ------------------------------------------------------------------
    std::cout << "\nSimulation complete.\n";
    std::cout << "  Final time : " << clock.current_time_ns_double() << " ns\n";
    std::cout << "  SNN  cycles: " << snn->cycle_count()  << "\n";
    std::cout << "  MCBA cycles: " << mcba->cycle_count() << "\n";

    return 0;
}