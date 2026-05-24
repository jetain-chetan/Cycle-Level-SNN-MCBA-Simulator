// test/test_clock.cpp — Functional test suite for the GlobalClock module.
//
// Tests are self-contained and use no external framework.
// Each test prints PASS or FAIL with a brief description.
//
// Build (from project root):
//   g++ -std=c++14 -O2 -Wall -Wextra -I sim/ -o test/test_clock test/test_clock.cpp
//
// Run:
//   ./test/test_clock

#include "global_clock.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_pass = 0, g_fail = 0;

#define TEST(name) void name()
#define RUN(name)  do { \
    std::cout << "  " << #name << " ... "; \
    try { name(); std::cout << "PASS\n"; ++g_pass; } \
    catch (const std::exception& e) { \
        std::cout << "FAIL  (" << e.what() << ")\n"; ++g_fail; } \
} while(0)

// Throw on failed assertion so the harness catches it
#define EXPECT(cond) \
    if (!(cond)) throw std::runtime_error("EXPECT failed: " #cond \
        "  [" __FILE__ ":" + std::to_string(__LINE__) + "]")

#define EXPECT_EQ(a, b) \
    if (!((a) == (b))) { \
        std::ostringstream _s; \
        _s << "EXPECT_EQ failed: " #a " == " #b \
           << "  (got " << (a) << " vs " << (b) << ")"; \
        throw std::runtime_error(_s.str()); }

#define EXPECT_NEAR(a, b, tol) \
    if (std::abs((a) - (b)) > (tol)) { \
        std::ostringstream _s; \
        _s << "EXPECT_NEAR failed: |" #a " - " #b "| <= " #tol \
           << "  (got |" << (a) << " - " << (b) << "| = " \
           << std::abs((a) - (b)) << ")"; \
        throw std::runtime_error(_s.str()); }

#define EXPECT_THROW(expr) \
    do { bool _threw = false; \
         try { expr; } catch (...) { _threw = true; } \
         if (!_threw) throw std::runtime_error( \
             "EXPECT_THROW: expected exception not thrown: " #expr); \
    } while(0)

// ---------------------------------------------------------------------------
// Section 1: Ratio arithmetic
// ---------------------------------------------------------------------------

TEST(ratio_construction_and_reduction) {
    Ratio r(6, 4);
    EXPECT_EQ(r.num, 3);
    EXPECT_EQ(r.den, 2);

    Ratio r2(-9, 6);
    EXPECT_EQ(r2.num, -3);
    EXPECT_EQ(r2.den, 2);

    // Negative denominator normalised
    Ratio r3(4, -6);
    EXPECT_EQ(r3.num, -2);
    EXPECT_EQ(r3.den, 3);
}

TEST(ratio_comparison) {
    EXPECT(Ratio(1,2) <  Ratio(3,4));
    EXPECT(Ratio(1,2) == Ratio(2,4));
    EXPECT(Ratio(3,4) >  Ratio(1,2));
    EXPECT(Ratio(1,3) != Ratio(1,4));
}

TEST(ratio_arithmetic) {
    Ratio a(1,3), b(1,6);
    Ratio sum = a + b;   // 1/3 + 1/6 = 1/2
    EXPECT_EQ(sum.num, 1);
    EXPECT_EQ(sum.den, 2);

    Ratio diff = a - b;  // 1/3 - 1/6 = 1/6
    EXPECT_EQ(diff.num, 1);
    EXPECT_EQ(diff.den, 6);

    Ratio prod = a * b;  // 1/3 * 1/6 = 1/18
    EXPECT_EQ(prod.num, 1);
    EXPECT_EQ(prod.den, 18);

    Ratio quot = a / b;  // (1/3) / (1/6) = 2
    EXPECT_EQ(quot.num, 2);
    EXPECT_EQ(quot.den, 1);
}

TEST(ratio_from_freq_mhz) {
    // 20 MHz -> period = 1000/20 = 50 ns
    Ratio p20 = Ratio::from_freq_mhz(20);
    EXPECT_EQ(p20.num, 50);
    EXPECT_EQ(p20.den, 1);

    // 250 MHz -> period = 1000/250 = 4 ns
    Ratio p250 = Ratio::from_freq_mhz(250);
    EXPECT_EQ(p250.num, 4);
    EXPECT_EQ(p250.den, 1);

    // 30 MHz -> period = 1000/30 = 100/3 ns (cannot be an integer)
    Ratio p30 = Ratio::from_freq_mhz(30);
    EXPECT_EQ(p30.num, 100);
    EXPECT_EQ(p30.den, 3);
}

TEST(ratio_zero_denominator_throws) {
    EXPECT_THROW(Ratio(1, 0));
}

// ---------------------------------------------------------------------------
// Section 2: TimeBase computation
// ---------------------------------------------------------------------------

TEST(timebase_integer_periods) {
    // 20 MHz (50 ns) and 250 MHz (4 ns) — both integer ns, D = LCM(1,1) = 1
    std::vector<Ratio> periods = { Ratio(50,1), Ratio(4,1) };
    TimeBase tb = compute_timebase(periods);
    EXPECT_EQ(tb.vt_per_ns, 1);
}

TEST(timebase_with_fractional_period) {
    // 30 MHz -> 100/3 ns. D = LCM(1, 3) = 3
    std::vector<Ratio> periods = {
        Ratio(50,  1),   // 20 MHz
        Ratio(4,   1),   // 250 MHz
        Ratio(100, 3),   // 30 MHz
    };
    TimeBase tb = compute_timebase(periods);
    EXPECT_EQ(tb.vt_per_ns, 3);
}

TEST(timebase_period_to_vt) {
    std::vector<Ratio> periods = { Ratio(50,1), Ratio(4,1), Ratio(100,3) };
    TimeBase tb = compute_timebase(periods);
    // D = 3
    EXPECT_EQ(tb.period_to_vt(Ratio(50,  1)), 150);  // 50   * 3 = 150
    EXPECT_EQ(tb.period_to_vt(Ratio(4,   1)),  12);  //  4   * 3 =  12
    EXPECT_EQ(tb.period_to_vt(Ratio(100, 3)), 100);  // 100/3 * 3 = 100
}

TEST(timebase_ns_to_vt_roundtrip) {
    std::vector<Ratio> periods = { Ratio(100,3) };
    TimeBase tb = compute_timebase(periods);
    EXPECT_EQ(tb.vt_per_ns, 3);

    int64_t vt = tb.ns_to_vt(100);   // 100 ns * 3 = 300 vt
    EXPECT_EQ(vt, 300);

    Ratio back = tb.vt_to_ns_ratio(vt);  // 300 / 3 = 100 ns
    EXPECT_EQ(back.num, 100);
    EXPECT_EQ(back.den, 1);
}

// ---------------------------------------------------------------------------
// Section 3: ClockDomain
// ---------------------------------------------------------------------------

TEST(clock_domain_finalize_sets_period_vt) {
    TimeBase tb{ 3 };   // 1 vt = 1/3 ns
    ClockDomain cd("TEST", Ratio(100, 3));
    cd.finalize(tb);
    EXPECT_EQ(cd.period_vt(), 100);
}

TEST(clock_domain_handlers_rejected_after_finalize) {
    TimeBase tb{ 1 };
    ClockDomain cd("TEST", Ratio(50, 1));
    cd.finalize(tb);
    EXPECT_THROW(cd.add_handler(TickPhase::SAMPLE, [](const Ratio&){}));
}

TEST(clock_domain_cycle_count_increments_on_commit_only) {
    TimeBase tb{ 1 };
    ClockDomain cd("TEST", Ratio(50, 1));
    cd.add_handler(TickPhase::SAMPLE, [](const Ratio&){});
    cd.add_handler(TickPhase::COMMIT, [](const Ratio&){});
    cd.finalize(tb);

    Ratio t(0,1);
    EXPECT_EQ(cd.cycle_count(), 0u);
    cd.fire(TickPhase::SAMPLE, t);
    EXPECT_EQ(cd.cycle_count(), 0u);   // SAMPLE must not increment
    cd.fire(TickPhase::COMMIT, t);
    EXPECT_EQ(cd.cycle_count(), 1u);   // COMMIT increments
}

// ---------------------------------------------------------------------------
// Section 4: GlobalClock — basic scheduling
// ---------------------------------------------------------------------------

TEST(global_clock_finalize_twice_throws) {
    GlobalClock clk;
    clk.register_unit_mhz("A", 100);
    clk.finalize();
    EXPECT_THROW(clk.finalize());
}

TEST(global_clock_register_after_finalize_throws) {
    GlobalClock clk;
    clk.register_unit_mhz("A", 100);
    clk.finalize();
    EXPECT_THROW(clk.register_unit_mhz("B", 50));
}

TEST(global_clock_empty_throws) {
    GlobalClock clk;
    EXPECT_THROW(clk.finalize());
}

TEST(global_clock_single_unit_tick_count) {
    // 250 MHz = 4 ns period. Over 100 ns: expect 25 ticks (t=0,4,8,...,96)
    // Note: t=0 fires because next_tick starts at 0.
    GlobalClock clk;
    ClockDomain* d = clk.register_unit_mhz("D", 250);
    int commit_count = 0;
    d->add_handler(TickPhase::COMMIT, [&](const Ratio&){ ++commit_count; });
    clk.finalize();
    clk.run(96);   // inclusive: ticks at 0,4,8,...,96 = 25 ticks
    EXPECT_EQ(commit_count, 25);
    EXPECT_EQ(d->cycle_count(), 25u);
}

TEST(global_clock_tick_timestamps_correct) {
    // 100 MHz = 10 ns period. Check that ticks fire at exact ns values.
    GlobalClock clk;
    ClockDomain* d = clk.register_unit_mhz("D", 100);
    std::vector<double> times;
    d->add_handler(TickPhase::COMMIT, [&](const Ratio& t){
        times.push_back(t.to_double());
    });
    clk.finalize();
    clk.run(40);

    // Expect ticks at 0, 10, 20, 30, 40 ns
    EXPECT_EQ(static_cast<int>(times.size()), 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_NEAR(times[i], i * 10.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Section 5: GlobalClock — multi-rate scheduling
// ---------------------------------------------------------------------------

TEST(global_clock_two_units_cycle_counts) {
    // SNN 20 MHz (50 ns), MCBA 250 MHz (4 ns), run for 200 ns.
    // SNN  ticks at 0, 50, 100, 150, 200      -> 5 ticks
    // MCBA ticks at 0, 4, 8, ..., 196, 200    -> 51 ticks
    GlobalClock clk;
    ClockDomain* snn  = clk.register_unit_mhz("SNN",  20);
    ClockDomain* mcba = clk.register_unit_mhz("MCBA", 250);
    snn ->add_handler(TickPhase::COMMIT, [](const Ratio&){});
    mcba->add_handler(TickPhase::COMMIT, [](const Ratio&){});
    clk.finalize();
    clk.run(200);

    EXPECT_EQ(snn ->cycle_count(), 5u);
    EXPECT_EQ(mcba->cycle_count(), 51u);
}

TEST(global_clock_30mhz_fractional_period) {
    // 30 MHz = 100/3 ns period. Over 300 ns: 9 + 1 = 10 ticks (t=0,100/3,...,300)
    // Actually ticks at 0, 100/3, 200/3, 100, 400/3, 500/3, 200, 700/3, 800/3, 300
    // = 10 ticks
    GlobalClock clk;
    ClockDomain* d = clk.register_unit_mhz("30M", 30);
    int count = 0;
    d->add_handler(TickPhase::COMMIT, [&](const Ratio&){ ++count; });
    clk.finalize();
    clk.run(300);
    EXPECT_EQ(count, 10);
}

TEST(global_clock_timebase_correct_for_30mhz) {
    GlobalClock clk;
    clk.register_unit_mhz("SNN",  20);
    clk.register_unit_mhz("MCBA", 250);
    clk.register_unit_mhz("30M",  30);
    clk.finalize();
    // D = LCM(1, 1, 3) = 3
    EXPECT_EQ(clk.timebase().vt_per_ns, 3);
}

// ---------------------------------------------------------------------------
// Section 6: Simultaneity — two-phase SAMPLE/COMMIT correctness
// ---------------------------------------------------------------------------

TEST(simultaneity_sample_before_commit) {
    // Both units tick at t=0. We track the order in which phases fire.
    // Expected sequence: SAMPLE_A, SAMPLE_B, COMMIT_A, COMMIT_B
    // (no COMMIT from any unit before all SAMPLEs are done)
    GlobalClock clk;
    ClockDomain* a = clk.register_unit_mhz("A", 100);  // 10 ns
    ClockDomain* b = clk.register_unit_mhz("B", 100);  // 10 ns (coincident)

    std::vector<std::string> log;
    a->add_handler(TickPhase::SAMPLE, [&](const Ratio&){ log.push_back("SAMPLE_A"); });
    b->add_handler(TickPhase::SAMPLE, [&](const Ratio&){ log.push_back("SAMPLE_B"); });
    a->add_handler(TickPhase::COMMIT, [&](const Ratio&){ log.push_back("COMMIT_A"); });
    b->add_handler(TickPhase::COMMIT, [&](const Ratio&){ log.push_back("COMMIT_B"); });

    clk.finalize();
    clk.step(clk.timebase().ns_to_vt(0));  // just the t=0 tick

    // All SAMPLEs must precede all COMMITs
    bool found_commit = false;
    for (const auto& entry : log) {
        if (entry.find("COMMIT") != std::string::npos)
            found_commit = true;
        if (entry.find("SAMPLE") != std::string::npos && found_commit)
            throw std::runtime_error(
                "SAMPLE fired after a COMMIT in the same tick batch");
    }
    EXPECT_EQ(static_cast<int>(log.size()), 4);
}

TEST(simultaneity_shared_state_consistency) {
    // Shared integer. Both units read it in SAMPLE, write in COMMIT.
    // After one coincident tick, each unit should have read the INITIAL
    // value (0), not any value written by the other unit.
    int shared = 0;
    int a_read = -1, b_read = -1;

    GlobalClock clk;
    ClockDomain* a = clk.register_unit_mhz("A", 100);
    ClockDomain* b = clk.register_unit_mhz("B", 100);

    a->add_handler(TickPhase::SAMPLE, [&](const Ratio&){ a_read = shared; });
    b->add_handler(TickPhase::SAMPLE, [&](const Ratio&){ b_read = shared; });
    a->add_handler(TickPhase::COMMIT, [&](const Ratio&){ shared += 10; });
    b->add_handler(TickPhase::COMMIT, [&](const Ratio&){ shared += 1;  });

    clk.finalize();
    clk.step(clk.timebase().ns_to_vt(0));

    // Both must have read 0 (initial state), not 10 or 1
    EXPECT_EQ(a_read, 0);
    EXPECT_EQ(b_read, 0);

    // After commit, shared = 11
    EXPECT_EQ(shared, 11);
}

// ---------------------------------------------------------------------------
// Section 7: Edge cases
// ---------------------------------------------------------------------------

TEST(coincident_ticks_at_lcm_boundary) {
    // SNN 20 MHz (50 ns) and MCBA 250 MHz (4 ns).
    // LCM(50, 4) = 200 ns — both tick at t=0 and t=200.
    GlobalClock clk;
    ClockDomain* snn  = clk.register_unit_mhz("SNN",  20);
    ClockDomain* mcba = clk.register_unit_mhz("MCBA", 250);

    std::vector<double> snn_times, mcba_times;
    snn ->add_handler(TickPhase::COMMIT, [&](const Ratio& t){ snn_times.push_back(t.to_double()); });
    mcba->add_handler(TickPhase::COMMIT, [&](const Ratio& t){ mcba_times.push_back(t.to_double()); });

    clk.finalize();
    clk.run(200);

    // SNN fires at 0, 50, 100, 150, 200
    EXPECT_EQ(static_cast<int>(snn_times.size()),  5);
    // MCBA fires at 0, 4, 8, ..., 200  = 51 ticks
    EXPECT_EQ(static_cast<int>(mcba_times.size()), 51);

    // Verify the LCM boundaries (t=0, t=200) appear in both
    EXPECT_NEAR(snn_times.front(),  0.0, 1e-12);
    EXPECT_NEAR(snn_times.back(), 200.0, 1e-12);
    EXPECT_NEAR(mcba_times.front(),  0.0, 1e-12);
    EXPECT_NEAR(mcba_times.back(), 200.0, 1e-12);
}

TEST(step_returns_false_past_stop_time) {
    GlobalClock clk;
    clk.register_unit_mhz("A", 100);
    clk.finalize();
    int64_t stop_vt = clk.timebase().ns_to_vt(0);  // only t=0
    clk.step(stop_vt);
    bool ret = clk.step(stop_vt);  // no more events <= t=0
    EXPECT(!ret);
}

TEST(single_tick_at_zero) {
    // Verify t=0 fires and is reported as 0 ns exactly
    GlobalClock clk;
    ClockDomain* d = clk.register_unit_mhz("A", 500);  // 2 ns period
    double fired_at = -1.0;
    d->add_handler(TickPhase::COMMIT, [&](const Ratio& t){ fired_at = t.to_double(); });
    clk.finalize();
    clk.step(clk.timebase().ns_to_vt(0));
    EXPECT_NEAR(fired_at, 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// Section 8: Verbose trace (not pass/fail — prints for visual inspection)
// ---------------------------------------------------------------------------

void verbose_trace() {
    std::cout << "\n  [Verbose trace: SNN 20 MHz + MCBA 250 MHz, first 20 ns]\n";

    GlobalClock clk;
    ClockDomain* snn  = clk.register_unit_mhz("SNN",  20);
    ClockDomain* mcba = clk.register_unit_mhz("MCBA", 250);

    snn->add_handler(TickPhase::SAMPLE, [](const Ratio& t){
        std::cout << "    [SAMPLE] SNN   @ " << t.to_double() << " ns\n";
    });
    snn->add_handler(TickPhase::COMMIT, [](const Ratio& t){
        std::cout << "    [COMMIT] SNN   @ " << t.to_double() << " ns\n";
    });
    mcba->add_handler(TickPhase::SAMPLE, [](const Ratio& t){
        std::cout << "    [SAMPLE] MCBA  @ " << t.to_double() << " ns\n";
    });
    mcba->add_handler(TickPhase::COMMIT, [](const Ratio& t){
        std::cout << "    [COMMIT] MCBA  @ " << t.to_double() << " ns\n";
    });

    clk.finalize();
    std::cout << "  " << clk.to_string() << "\n";
    clk.run(20);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << "=== Clock module tests ===\n\n";

    std::cout << "Ratio arithmetic:\n";
    RUN(ratio_construction_and_reduction);
    RUN(ratio_comparison);
    RUN(ratio_arithmetic);
    RUN(ratio_from_freq_mhz);
    RUN(ratio_zero_denominator_throws);

    std::cout << "\nTimeBase:\n";
    RUN(timebase_integer_periods);
    RUN(timebase_with_fractional_period);
    RUN(timebase_period_to_vt);
    RUN(timebase_ns_to_vt_roundtrip);

    std::cout << "\nClockDomain:\n";
    RUN(clock_domain_finalize_sets_period_vt);
    RUN(clock_domain_handlers_rejected_after_finalize);
    RUN(clock_domain_cycle_count_increments_on_commit_only);

    std::cout << "\nGlobalClock — basic scheduling:\n";
    RUN(global_clock_finalize_twice_throws);
    RUN(global_clock_register_after_finalize_throws);
    RUN(global_clock_empty_throws);
    RUN(global_clock_single_unit_tick_count);
    RUN(global_clock_tick_timestamps_correct);

    std::cout << "\nGlobalClock — multi-rate:\n";
    RUN(global_clock_two_units_cycle_counts);
    RUN(global_clock_30mhz_fractional_period);
    RUN(global_clock_timebase_correct_for_30mhz);

    std::cout << "\nSimultaneity:\n";
    RUN(simultaneity_sample_before_commit);
    RUN(simultaneity_shared_state_consistency);

    std::cout << "\nEdge cases:\n";
    RUN(coincident_ticks_at_lcm_boundary);
    RUN(step_returns_false_past_stop_time);
    RUN(single_tick_at_zero);

    verbose_trace();

    std::cout << "\n=== Results: "
              << g_pass << " passed, "
              << g_fail << " failed ===\n";

    return g_fail > 0 ? 1 : 0;
}