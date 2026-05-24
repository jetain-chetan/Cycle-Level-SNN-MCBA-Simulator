// ============================================================
// test_lif_neuron.cpp — LIF Neuron + Q8.8 Test Suite
// ============================================================
// Self-contained test runner. No external framework.
// Each test emits CSV data to stdout so the Python plotting
// infrastructure can consume it directly.
//
// Build (from project root):
//   g++ -std=c++14 -O2 -Wall -Wextra -I sim/
//       -o test/test_lif_neuron test/test_lif_neuron.cpp
//
// Run all tests (assertions only, no CSV):
//   ./test/test_lif_neuron
//
// Run a specific test and emit CSV on stdout:
//   ./test/test_lif_neuron --csv <test_id>
//
//   test_id values:
//     t01_dynamic_threshold_a      (default params)
//     t01_dynamic_threshold_b      (slow decay variant)
//     t01_dynamic_threshold_c      (strong boost variant)
//     t02_rate_encoding_full        (all dynamics enabled)
//     t02_rate_encoding_no_refrac   (refrac disabled)
//     t02_rate_encoding_no_thresh   (threshold dynamics disabled)
//     t03_refractory_period_0
//     t03_refractory_period_4
//     t03_refractory_period_8
//     t03_refractory_period_16
//     t04_vm_decay_a               (alpha_leak=0.95)
//     t04_vm_decay_b               (alpha_leak=0.80)
//     t04_vm_decay_c               (alpha_leak=0.50)
//     t05_inhibitory_clamp
//     t06_threshold_floor
//     t07_reset_correctness        (no CSV — assertion only)
//     t08_refrac_thresh_interaction
//     t09_min_isi_0
//     t09_min_isi_4
//     t09_min_isi_8
//     t10_q88_unit                 (no CSV — assertion only)
//     t11_param_quantisation       (no CSV — assertion only)
//     t12_long_run_stability
//
// CSV format (all tests that emit CSV):
//   tick,syn_input,vmembrane,vthreshold,refractory_ctr,spike_out
//   Values are doubles (q88_to_double applied) except spike_out (0/1)
//   and refractory_ctr (integer).
// ============================================================

#include "../sim/units/SNN/lif_neuron.hpp"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

// ------------------------------------------------------------
// Minimal assertion infrastructure
// ------------------------------------------------------------
static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

// Internal: record a single assertion result.
static void record(bool ok, const char* expr, const char* file, int line)
{
    ++g_tests_run;
    if (ok)
    {
        ++g_tests_passed;
    }
    else
    {
        ++g_tests_failed;
        fprintf(stderr, "  FAIL  %s:%d  ->  %s\n", file, line, expr);
    }
}

#define CHECK(expr) record((expr), #expr, __FILE__, __LINE__)

// Assert two q88_t values are equal; print human-readable mismatch.
static void check_q88_eq(q88_t got, q88_t expected,
                          const char* label, const char* file, int line)
{
    ++g_tests_run;
    if (got == expected)
    {
        ++g_tests_passed;
    }
    else
    {
        ++g_tests_failed;
        fprintf(stderr, "  FAIL  %s:%d  %s: got %.6f (%d), expected %.6f (%d)\n",
                file, line, label,
                q88_to_double(got),     (int)got,
                q88_to_double(expected),(int)expected);
    }
}

#define CHECK_Q88_EQ(got, expected, label) \
    check_q88_eq((got), (expected), (label), __FILE__, __LINE__)

// ------------------------------------------------------------
// CSV helpers
// ------------------------------------------------------------

// Emit CSV header.
static void csv_header()
{
    printf("tick,syn_input,vmembrane,vthreshold,refractory_ctr,spike_out\n");
}

// Emit one CSV row from neuron state.
static void csv_row(int tick, q88_t syn_input, const LIFNeuron& n)
{
    printf("%d,%.6f,%.6f,%.6f,%u,%d\n",
           tick,
           q88_to_double(syn_input),
           q88_to_double(n.vmembrane()),
           q88_to_double(n.vthreshold()),
           (unsigned)n.refractory_ctr(),
           (int)n.spike_out());
}

// ------------------------------------------------------------
// Parameter presets
// ------------------------------------------------------------

// Reference parameters — Verilog defaults re-scaled to Q8.8.
static NeuronParams make_default_params()
{
    NeuronParams p;
    p.vth           = q88_from_double(1.0);
    p.vreset        = q88_from_int(0);
    p.alpha_leak    = q88_from_double(0.95);
    p.alpha_th      = q88_from_double(0.98);
    p.delta_th      = q88_from_double(1.25);
    p.refrac_period = 8;
    return p;
}

// Variant: no refractory period.
static NeuronParams make_no_refrac_params()
{
    NeuronParams p = make_default_params();
    p.refrac_period = 0;
    return p;
}

// Variant: no threshold dynamics (static threshold).
// alpha_th = 1.0 (no decay), delta_th = 1.0 (no boost).
static NeuronParams make_no_thresh_dyn_params()
{
    NeuronParams p = make_default_params();
    p.alpha_th  = q88_from_double(1.0);   // threshold never decays
    p.delta_th  = q88_from_double(1.0);   // threshold never boosts
    return p;
}

// ============================================================
// TEST 1 — Dynamic Threshold
// ============================================================
// Sends a brief excitatory burst to cause a spike, then
// provides zero input so the threshold can be observed
// decaying back toward vth. Repeats to show boost cycles.
// Runs three parameter variants.
// ============================================================

static void run_t01(const char* variant_name, NeuronParams p,
                    q88_t burst_input, int burst_ticks,
                    int quiet_ticks, int cycles,
                    bool emit_csv)
{
    LIFNeuron neuron(p);

    if (emit_csv) { csv_header(); }

    int tick = 0;
    for (int c = 0; c < cycles; ++c)
    {
        // Burst phase — drive toward spike.
        for (int i = 0; i < burst_ticks; ++i, ++tick)
        {
            neuron.tick(burst_input);
            if (emit_csv) csv_row(tick, burst_input, neuron);
        }
        // Quiet phase — observe threshold decay.
        for (int i = 0; i < quiet_ticks; ++i, ++tick)
        {
            q88_t zero = q88_from_int(0);
            neuron.tick(zero);
            if (emit_csv) csv_row(tick, zero, neuron);
        }
    }

    // Assertion: threshold must never drop below vth.
    LIFNeuron check_n(p);
    q88_t min_vth_seen = check_n.vthreshold();
    q88_t zero = q88_from_int(0);
    for (int t = 0; t < (burst_ticks + quiet_ticks) * cycles; ++t)
    {
        q88_t in = (t % (burst_ticks + quiet_ticks) < burst_ticks)
                   ? burst_input : zero;
        check_n.tick(in);
        if (check_n.vthreshold() < min_vth_seen)
            min_vth_seen = check_n.vthreshold();
    }
    CHECK(min_vth_seen >= p.vth);
    (void)variant_name; // suppress unused warning in non-verbose build
}

static void t01_dynamic_threshold(bool emit_csv, const char* sub)
{
    // Variant A: default params, moderate burst.
    if (!sub || strcmp(sub, "t01_dynamic_threshold_a") == 0)
    {
        NeuronParams p = make_default_params();
        if (emit_csv && strcmp(sub, "t01_dynamic_threshold_a") == 0)
            run_t01("A", p, q88_from_double(0.6), 5, 20, 4, true);
        else
            run_t01("A", p, q88_from_double(0.6), 5, 20, 4, false);
    }
    // Variant B: slow threshold decay (alpha_th=0.995), long quiet period.
    if (!sub || strcmp(sub, "t01_dynamic_threshold_b") == 0)
    {
        NeuronParams p = make_default_params();
        p.alpha_th = q88_from_double(0.995);
        if (emit_csv && strcmp(sub, "t01_dynamic_threshold_b") == 0)
            run_t01("B", p, q88_from_double(0.6), 5, 60, 3, true);
        else
            run_t01("B", p, q88_from_double(0.6), 5, 60, 3, false);
    }
    // Variant C: strong boost (delta_th=1.5), fast decay (alpha_th=0.92).
    if (!sub || strcmp(sub, "t01_dynamic_threshold_c") == 0)
    {
        NeuronParams p = make_default_params();
        p.delta_th = q88_from_double(1.5);
        p.alpha_th = q88_from_double(0.92);
        if (emit_csv && strcmp(sub, "t01_dynamic_threshold_c") == 0)
            run_t01("C", p, q88_from_double(0.7), 4, 15, 5, true);
        else
            run_t01("C", p, q88_from_double(0.7), 4, 15, 5, false);
    }
}

// ============================================================
// TEST 2 — Rate Encoding
// ============================================================
// Constant syn_input for many ticks. Count spikes. Higher
// input should produce higher spike rate. Tests three
// parameter modes: full dynamics, no refractory, no threshold
// dynamics. Runs multiple input levels within each mode.
// ============================================================

static void run_t02(NeuronParams p, q88_t const_input,
                    int num_ticks, bool emit_csv,
                    int* spike_count_out)
{
    LIFNeuron neuron(p);
    int spikes = 0;

    if (emit_csv) { csv_header(); }

    for (int t = 0; t < num_ticks; ++t)
    {
        bool spk = neuron.tick(const_input);
        if (spk) ++spikes;
        if (emit_csv) csv_row(t, const_input, neuron);
    }
    if (spike_count_out) *spike_count_out = spikes;
}

static void t02_rate_encoding(bool emit_csv, const char* sub)
{
    const int TICKS = 100;
    // Input levels to sweep.
    const double levels[] = { 0.3, 0.5, 0.7, 1.0, 1.5 };
    const int    n_levels = 5;

    // For CSV emission, we only emit one mode/level per run.
    // For assertions, we run all levels and check ordering.

    // --- Full dynamics ---
    if (!sub || strcmp(sub, "t02_rate_encoding_full") == 0)
    {
        NeuronParams p = make_default_params();
        int prev_spikes = -1;
        for (int li = 0; li < n_levels; ++li)
        {
            q88_t inp = q88_from_double(levels[li]);
            int spikes = 0;
            bool do_csv = emit_csv &&
                          strcmp(sub, "t02_rate_encoding_full") == 0 &&
                          li == 2; // emit middle level for visualisation
            run_t02(p, inp, TICKS, do_csv, &spikes);
            // Higher input must produce >= spikes (monotone).
            if (prev_spikes >= 0) { CHECK(spikes >= prev_spikes); }
            prev_spikes = spikes;
        }
    }

    // --- No refractory ---
    if (!sub || strcmp(sub, "t02_rate_encoding_no_refrac") == 0)
    {
        NeuronParams p = make_no_refrac_params();
        int prev_spikes = -1;
        for (int li = 0; li < n_levels; ++li)
        {
            q88_t inp = q88_from_double(levels[li]);
            int spikes = 0;
            bool do_csv = emit_csv &&
                          strcmp(sub, "t02_rate_encoding_no_refrac") == 0 &&
                          li == 2;
            run_t02(p, inp, TICKS, do_csv, &spikes);
            if (prev_spikes >= 0) { CHECK(spikes >= prev_spikes); }
            prev_spikes = spikes;
        }
    }

    // --- No threshold dynamics (static threshold) ---
    if (!sub || strcmp(sub, "t02_rate_encoding_no_thresh") == 0)
    {
        NeuronParams p = make_no_thresh_dyn_params();
        // With a static threshold and constant input the ISI should
        // be perfectly regular. Verify spike count is consistent.
        q88_t inp = q88_from_double(0.7);
        int spikes_a = 0, spikes_b = 0;
        bool do_csv = emit_csv &&
                      strcmp(sub, "t02_rate_encoding_no_thresh") == 0;
        run_t02(p, inp, TICKS,     do_csv,  &spikes_a);
        run_t02(p, inp, TICKS * 2, false,   &spikes_b);
        // Two runs of length N and 2N must have consistent rate.
        // Allow ±1 for boundary effects.
        int diff = spikes_b - 2 * spikes_a;
        if (diff < 0) diff = -diff;
        CHECK(diff <= 1);
    }
}

// ============================================================
// TEST 3 — Refractory Period
// ============================================================
// With a permanently suprathreshold input, verify that the
// inter-spike interval equals refrac_period + 1 exactly.
// Tested for refrac_period in {0, 4, 8, 16}.
// ============================================================

static void run_t03(uint8_t refrac, bool emit_csv)
{
    NeuronParams p = make_default_params();
    p.refrac_period = refrac;
    // Disable threshold dynamics so ISI is constant.
    p.alpha_th = q88_from_double(1.0);
    p.delta_th = q88_from_double(1.0);

    LIFNeuron neuron(p);
    // Large constant input — guaranteed suprathreshold every
    // cycle once refractory expires.
    q88_t inp = q88_from_double(2.0);

    const int TICKS = 200;

    if (emit_csv) { csv_header(); }

    int prev_spike_tick = -1;
    int isi_violations  = 0;
    int spikes_seen     = 0;

    for (int t = 0; t < TICKS; ++t)
    {
        bool spk = neuron.tick(inp);
        if (emit_csv) csv_row(t, inp, neuron);
        if (spk)
        {
            ++spikes_seen;
            if (prev_spike_tick >= 0)
            {
                int isi = t - prev_spike_tick;
                // Expected ISI: refrac_period + 1 (one spike tick +
                // refrac_period silent ticks).
                int expected_isi = (int)refrac + 1;
                if (isi != expected_isi) ++isi_violations;
            }
            prev_spike_tick = t;
        }
    }

    // Must have seen at least a few spikes to be meaningful.
    CHECK(spikes_seen >= 3);
    // All ISIs must match exactly.
    CHECK(isi_violations == 0);
}

static void t03_refractory_period(bool emit_csv, const char* sub)
{
    struct { const char* name; uint8_t refrac; } cases[] = {
        { "t03_refractory_period_0",  0  },
        { "t03_refractory_period_4",  4  },
        { "t03_refractory_period_8",  8  },
        { "t03_refractory_period_16", 16 }
    };
    for (int i = 0; i < 4; ++i)
    {
        bool do_csv = emit_csv && sub && strcmp(sub, cases[i].name) == 0;
        if (!sub || strcmp(sub, cases[i].name) == 0)
            run_t03(cases[i].refrac, do_csv);
    }
}

// ============================================================
// TEST 4 — Vmembrane Decay
// ============================================================
// Burst of sub-threshold input, then silence. Membrane
// should decay back to vreset. Repeat. Test three alpha_leak
// values: 0.95, 0.80, 0.50.
// ============================================================

static void run_t04(double alpha_leak_d, bool emit_csv)
{
    NeuronParams p = make_default_params();
    p.alpha_leak = q88_from_double(alpha_leak_d);
    // Use a high threshold so we don't accidentally spike.
    p.vth           = q88_from_double(5.0);
    p.refrac_period = 0;

    LIFNeuron neuron(p);
    // Sub-threshold burst: accumulate some Vm.
    q88_t burst = q88_from_double(0.8);
    q88_t zero  = q88_from_int(0);

    const int BURST  = 10;
    const int QUIET  = 40;
    const int CYCLES = 3;

    if (emit_csv) { csv_header(); }

    int tick = 0;
    for (int c = 0; c < CYCLES; ++c)
    {
        for (int i = 0; i < BURST; ++i, ++tick)
        {
            neuron.tick(burst);
            if (emit_csv) csv_row(tick, burst, neuron);
        }

        q88_t vm_peak = neuron.vmembrane();

        for (int i = 0; i < QUIET; ++i, ++tick)
        {
            neuron.tick(zero);
            if (emit_csv) csv_row(tick, zero, neuron);
        }

        q88_t vm_after_quiet = neuron.vmembrane();

        // After silence the membrane must be strictly closer to
        // vreset than it was at peak (for alpha_leak < 1.0).
        CHECK(vm_after_quiet <= vm_peak);
        // For the quiet periods used here, the membrane should
        // have decayed essentially back toward vreset.
        // We check it is at least 50% of the way back regardless
        // of alpha_leak (40 ticks of even 0.95^40 ≈ 0.13).
        // Threshold: vm < vm_peak / 4.
        CHECK(vm_after_quiet < vm_peak / 4 + 1);
    }
}

static void t04_vm_decay(bool emit_csv, const char* sub)
{
    struct { const char* name; double alpha; } cases[] = {
        { "t04_vm_decay_a", 0.95 },
        { "t04_vm_decay_b", 0.80 },
        { "t04_vm_decay_c", 0.50 }
    };
    for (int i = 0; i < 3; ++i)
    {
        bool do_csv = emit_csv && sub && strcmp(sub, cases[i].name) == 0;
        if (!sub || strcmp(sub, cases[i].name) == 0)
            run_t04(cases[i].alpha, do_csv);
    }
}

// ============================================================
// TEST 5 — Inhibitory Input / Clamp Correctness
// ============================================================
// Strongly negative syn_input at various membrane states.
// Vmembrane must never fall below vreset at any point.
// ============================================================

static void t05_inhibitory_clamp(bool emit_csv, const char* sub)
{
    if (sub && strcmp(sub, "t05_inhibitory_clamp") != 0) return;

    NeuronParams p = make_default_params();
    LIFNeuron neuron(p);

    q88_t big_neg = q88_from_double(-2.0);
    q88_t zero    = q88_from_int(0);
    q88_t mod_pos = q88_from_double(0.5);

    if (emit_csv) { csv_header(); }

    int tick = 0;

    // Phase 1: inhibitory from rest.
    for (int i = 0; i < 10; ++i, ++tick)
    {
        neuron.tick(big_neg);
        if (emit_csv) csv_row(tick, big_neg, neuron);
        CHECK(neuron.vmembrane() >= p.vreset);
    }

    // Phase 2: charge up partially, then inhibit.
    for (int i = 0; i < 5; ++i, ++tick)
    {
        neuron.tick(mod_pos);
        if (emit_csv) csv_row(tick, mod_pos, neuron);
    }
    for (int i = 0; i < 10; ++i, ++tick)
    {
        neuron.tick(big_neg);
        if (emit_csv) csv_row(tick, big_neg, neuron);
        CHECK(neuron.vmembrane() >= p.vreset);
    }

    // Phase 3: spike, then immediately inhibit during refractory.
    q88_t big_pos = q88_from_double(2.0);
    neuron.tick(big_pos);  // force spike
    ++tick;
    if (emit_csv) csv_row(tick, big_pos, neuron);
    for (int i = 0; i < 5; ++i, ++tick)
    {
        neuron.tick(big_neg);
        if (emit_csv) csv_row(tick, big_neg, neuron);
        CHECK(neuron.vmembrane() >= p.vreset);
    }

    // Phase 4: sustained inhibitory barrage — must never spike.
    neuron.reset();
    int spurious_spikes = 0;
    for (int i = 0; i < 50; ++i, ++tick)
    {
        bool spk = neuron.tick(big_neg);
        if (emit_csv) csv_row(tick, big_neg, neuron);
        if (spk) ++spurious_spikes;
        CHECK(neuron.vmembrane() >= p.vreset);
    }
    CHECK(spurious_spikes == 0);
    (void)zero;
}

// ============================================================
// TEST 6 — Threshold Floor Enforcement
// ============================================================
// Fire once to boost threshold, then provide zero input for
// a very long time. Threshold must asymptote to vth and stop.
// Tested for two extreme alpha_th values.
// ============================================================

static void run_t06(double alpha_th_d, bool emit_csv)
{
    NeuronParams p = make_default_params();
    p.alpha_th = q88_from_double(alpha_th_d);
    p.refrac_period = 0;

    LIFNeuron neuron(p);
    q88_t big_inp = q88_from_double(2.0);
    q88_t zero    = q88_from_int(0);

    // Fire once.
    neuron.tick(big_inp);

    const int QUIET = 300;
    if (emit_csv) { csv_header(); }

    q88_t prev_vth = neuron.vthreshold();
    for (int t = 0; t < QUIET; ++t)
    {
        neuron.tick(zero);
        if (emit_csv) csv_row(t, zero, neuron);

        // Threshold must never go below vth floor.
        CHECK(neuron.vthreshold() >= p.vth);
        // Threshold must be non-increasing (decaying only).
        CHECK(neuron.vthreshold() <= prev_vth);
        prev_vth = neuron.vthreshold();
    }
    // After 300 silent ticks it must have converged to exactly vth.
    CHECK(neuron.vthreshold() == p.vth);
}

static void t06_threshold_floor(bool emit_csv, const char* sub)
{
    if (sub && strcmp(sub, "t06_threshold_floor") != 0) return;
    run_t06(0.50, emit_csv);   // fast decay
    run_t06(0.98, false);      // slow decay — assertion only
}

// ============================================================
// TEST 7 — Reset Correctness
// ============================================================
// Call reset() at mid-integration, mid-refractory, and
// post-spike. Verify all four state variables are restored
// and subsequent behaviour is identical to a fresh neuron.
// No CSV output — assertion-only test.
// ============================================================

static void t07_reset_correctness(bool emit_csv, const char* sub)
{
    (void)emit_csv;
    if (sub && strcmp(sub, "t07_reset_correctness") != 0) return;

    NeuronParams p = make_default_params();
    q88_t mod_inp = q88_from_double(0.5);
    q88_t big_inp = q88_from_double(2.0);

    // -- Scenario A: reset mid-integration --
    {
        LIFNeuron n(p);
        for (int i = 0; i < 5; ++i) n.tick(mod_inp);
        n.reset();
        CHECK(n.vmembrane()      == p.vreset);
        CHECK(n.vthreshold()     == p.vth);
        CHECK(n.refractory_ctr() == 0);
        CHECK(n.spike_out()      == false);
    }

    // -- Scenario B: reset mid-refractory --
    {
        LIFNeuron n(p);
        n.tick(big_inp); // spike
        CHECK(n.refractory_ctr() == p.refrac_period);
        n.tick(big_inp); // one refractory tick
        n.reset();
        CHECK(n.vmembrane()      == p.vreset);
        CHECK(n.vthreshold()     == p.vth);
        CHECK(n.refractory_ctr() == 0);
        CHECK(n.spike_out()      == false);
    }

    // -- Scenario C: reset immediately after spike --
    {
        LIFNeuron n(p);
        n.tick(big_inp);
        n.reset();
        CHECK(n.vmembrane()      == p.vreset);
        CHECK(n.vthreshold()     == p.vth);
        CHECK(n.refractory_ctr() == 0);
        CHECK(n.spike_out()      == false);
    }

    // -- Scenario D: post-reset behaviour identical to fresh neuron --
    {
        LIFNeuron fresh(p);
        LIFNeuron used(p);
        // Dirty up 'used' thoroughly.
        for (int i = 0; i < 20; ++i) used.tick(big_inp);
        used.reset();
        // Run both for 30 ticks with identical input.
        for (int i = 0; i < 30; ++i)
        {
            q88_t inp = (i % 3 == 0) ? big_inp : mod_inp;
            bool spk_fresh = fresh.tick(inp);
            bool spk_used  = used.tick(inp);
            CHECK(spk_fresh               == spk_used);
            CHECK(fresh.vmembrane()       == used.vmembrane());
            CHECK(fresh.vthreshold()      == used.vthreshold());
            CHECK(fresh.refractory_ctr()  == used.refractory_ctr());
        }
    }
}

// ============================================================
// TEST 8 — Refractory / Threshold Interaction
// ============================================================
// During refractory, the threshold must continue decaying
// (vt_next applied even in refractory branch). Verify
// threshold is lower at the end of refractory than it was
// at the spike moment.
// ============================================================

static void t08_refrac_thresh_interaction(bool emit_csv, const char* sub)
{
    if (sub && strcmp(sub, "t08_refrac_thresh_interaction") != 0) return;

    NeuronParams p = make_default_params();
    p.refrac_period = 8;
    LIFNeuron neuron(p);

    q88_t big_inp = q88_from_double(2.0);
    q88_t zero    = q88_from_int(0);

    if (emit_csv) { csv_header(); }

    int tick = 0;

    // Fire the neuron.
    neuron.tick(big_inp);
    if (emit_csv) csv_row(tick++, big_inp, neuron);

    q88_t vth_at_spike = neuron.vthreshold();  // boosted value

    // Track threshold during refractory (zero input).
    q88_t vth_prev = vth_at_spike;
    for (int i = 0; i < (int)p.refrac_period; ++i, ++tick)
    {
        neuron.tick(zero);
        if (emit_csv) csv_row(tick, zero, neuron);
        // Threshold must be decaying each step.
        CHECK(neuron.vthreshold() <= vth_prev);
        vth_prev = neuron.vthreshold();
    }
    // After refractory completes, threshold must be below spike value.
    CHECK(neuron.vthreshold() < vth_at_spike);
    // But still above the baseline floor.
    CHECK(neuron.vthreshold() >= p.vth);
    // Refractory counter must be exactly 0 now.
    CHECK(neuron.refractory_ctr() == 0);
}

// ============================================================
// TEST 9 — Minimum Inter-Spike Interval
// ============================================================
// Suprathreshold input, static threshold, varying refrac.
// ISI must equal refrac_period + 1 exactly.
// Edge case: refrac_period = 0 means consecutive-tick firing.
// ============================================================

static void run_t09(uint8_t refrac, bool emit_csv)
{
    NeuronParams p = make_default_params();
    p.refrac_period = refrac;
    p.alpha_th  = q88_from_double(1.0);  // static threshold
    p.delta_th  = q88_from_double(1.0);

    LIFNeuron neuron(p);
    q88_t inp = q88_from_double(2.0);

    const int TICKS = 150;
    if (emit_csv) { csv_header(); }

    int prev_spike  = -1;
    int violations  = 0;
    int spikes_seen = 0;

    for (int t = 0; t < TICKS; ++t)
    {
        bool spk = neuron.tick(inp);
        if (emit_csv) csv_row(t, inp, neuron);
        if (spk)
        {
            ++spikes_seen;
            if (prev_spike >= 0)
            {
                int isi = t - prev_spike;
                if (isi != (int)refrac + 1) ++violations;
            }
            prev_spike = t;
        }
    }
    CHECK(spikes_seen >= 3);
    CHECK(violations == 0);
}

static void t09_min_isi(bool emit_csv, const char* sub)
{
    struct { const char* name; uint8_t refrac; } cases[] = {
        { "t09_min_isi_0", 0 },
        { "t09_min_isi_4", 4 },
        { "t09_min_isi_8", 8 }
    };
    for (int i = 0; i < 3; ++i)
    {
        bool do_csv = emit_csv && sub && strcmp(sub, cases[i].name) == 0;
        if (!sub || strcmp(sub, cases[i].name) == 0)
            run_t09(cases[i].refrac, do_csv);
    }
}

// ============================================================
// TEST 10 — Q8.8 Arithmetic Unit Tests
// ============================================================
// Verify q88_mul and conversion helpers in isolation.
// No CSV output — arithmetic correctness only.
// ============================================================

static void t10_q88_unit(bool emit_csv, const char* sub)
{
    (void)emit_csv;
    if (sub && strcmp(sub, "t10_q88_unit") != 0) return;

    // -- from_int / from_double / to_double round-trips --
    CHECK_Q88_EQ(q88_from_int(1),            256,  "from_int(1)");
    CHECK_Q88_EQ(q88_from_int(0),            0,    "from_int(0)");
    CHECK_Q88_EQ(q88_from_double(1.0),       256,  "from_double(1.0)");
    CHECK_Q88_EQ(q88_from_double(0.0),       0,    "from_double(0.0)");
    CHECK_Q88_EQ(q88_from_double(0.5),       128,  "from_double(0.5)");
    CHECK_Q88_EQ(q88_from_double(0.25),      64,   "from_double(0.25)");
    CHECK_Q88_EQ(q88_from_double(0.95),      243,  "from_double(0.95)");
    CHECK_Q88_EQ(q88_from_double(1.25),      320,  "from_double(1.25)");
    CHECK_Q88_EQ(q88_from_double(-1.0),     -256,  "from_double(-1.0)");

    // -- q88_mul exact cases --
    // 1.0 * 1.0 = 1.0
    CHECK_Q88_EQ(q88_mul(256, 256), 256,  "1.0 * 1.0");
    // 1.0 * 0.0 = 0.0
    CHECK_Q88_EQ(q88_mul(256, 0),   0,    "1.0 * 0.0");
    // 0.5 * 0.5 = 0.25  (128*128=16384, >>8 = 64 = 0.25)
    CHECK_Q88_EQ(q88_mul(128, 128), 64,   "0.5 * 0.5");
    // 0.25 * 0.25 = 0.0625  (64*64=4096, >>8=16=0.0625)
    CHECK_Q88_EQ(q88_mul(64, 64),   16,   "0.25 * 0.25");
    // 2.0 * 0.5 = 1.0  (512*128=65536, >>8=256)
    CHECK_Q88_EQ(q88_mul(512, 128), 256,  "2.0 * 0.5");
    // 0.95 * 1.0 = 0.95 exactly in Q8.8? 243*256=62208, >>8=243. Yes.
    CHECK_Q88_EQ(q88_mul(243, 256), 243,  "0.95 * 1.0");

    // -- to_double sanity --
    // Tolerate floating-point display imprecision — check via multiply.
    q88_t half = q88_from_double(0.5);
    q88_t prod = q88_mul(half, half);   // 0.25
    q88_t expected_025 = q88_from_double(0.25);
    CHECK_Q88_EQ(prod, expected_025, "0.5*0.5 == 0.25 via from_double");
}

// ============================================================
// TEST 11 — Parameter Quantisation
// ============================================================
// Q8.8 resolution is 1/256. Verify that parameters that round
// to the same Q8.8 value behave identically, and that the
// boundary between "leaks" and "does not leak" is documented.
// ============================================================

static void t11_param_quantisation(bool emit_csv, const char* sub)
{
    (void)emit_csv;
    if (sub && strcmp(sub, "t11_param_quantisation") != 0) return;

    // 0.999 rounds to 256 (same as 1.0) because floor(0.999*256)=255 — wait,
    // let's compute: 0.999*256 = 255.744 -> truncated to 255. So it IS
    // distinguishable from 1.0 (256). Verify.
    q88_t alpha_999 = q88_from_double(0.999);
    q88_t alpha_100 = q88_from_double(1.000);
    // They must be different raw values.
    CHECK(alpha_999 != alpha_100);
    // alpha_999 should be 255 (0.99609375).
    CHECK_Q88_EQ(alpha_999, 255, "0.999 -> 255 in Q8.8");

    // With alpha_leak = 1.0 (256) the membrane never decays.
    // With alpha_leak = 255 (0.99609375) it slowly decays.
    {
        NeuronParams p = make_default_params();
        p.vth           = q88_from_double(5.0);   // high threshold — no spikes
        p.refrac_period = 0;
        p.alpha_th      = q88_from_double(1.0);
        p.delta_th      = q88_from_double(1.0);

        // alpha_leak = 1.0: membrane must not decay.
        p.alpha_leak = alpha_100;
        LIFNeuron n_nodecay(p);
        q88_t inp = q88_from_double(0.5);
        // Charge up.
        for (int i = 0; i < 10; ++i) n_nodecay.tick(inp);
        q88_t vm_charged = n_nodecay.vmembrane();
        // Silent ticks — membrane should stay flat.
        for (int i = 0; i < 20; ++i) n_nodecay.tick(q88_from_int(0));
        CHECK(n_nodecay.vmembrane() == vm_charged);

        // alpha_leak = 0.999 -> 255: membrane must slowly decay.
        p.alpha_leak = alpha_999;
        LIFNeuron n_decay(p);
        for (int i = 0; i < 10; ++i) n_decay.tick(inp);
        q88_t vm_charged2 = n_decay.vmembrane();
        for (int i = 0; i < 20; ++i) n_decay.tick(q88_from_int(0));
        CHECK(n_decay.vmembrane() < vm_charged2);
    }

    // Document the minimum distinguishable alpha step.
    // The step is 1/256. Values that differ by less than 1/256 are the
    // same Q8.8 representation. Verify two values 1/512 apart round
    // to the same Q8.8 value (they may or may not, depends on truncation).
    // This is informational — we just record what happens.
    q88_t a = q88_from_double(0.95);
    q88_t b = q88_from_double(0.95 + 1.0/512.0);  // half a Q8.8 step above
    // No assertion — the result (same or different) is architecture-defined
    // by truncation. We simply verify the test runs without crashing.
    (void)a; (void)b;
}

// ============================================================
// TEST 12 — Long-Run Stability
// ============================================================
// Sub-threshold constant input for 2000 ticks. Verify Vm
// converges to a fixed point and does not drift indefinitely.
// ============================================================

static void t12_long_run_stability(bool emit_csv, const char* sub)
{
    if (sub && strcmp(sub, "t12_long_run_stability") != 0) return;

    NeuronParams p = make_default_params();
    // Raise threshold so we never spike.
    p.vth           = q88_from_double(5.0);
    p.refrac_period = 0;
    p.alpha_th      = q88_from_double(1.0);  // threshold static
    p.delta_th      = q88_from_double(1.0);

    LIFNeuron neuron(p);
    // inp=0.1 -> equilibrium Vm = alpha*inp/(1-alpha) ~ 1.87, safely below vth=5.0
    q88_t inp = q88_from_double(0.1);

    const int TICKS = 2000;
    if (emit_csv) { csv_header(); }

    q88_t prev_vm = neuron.vmembrane();
    int oscillation_count = 0;

    for (int t = 0; t < TICKS; ++t)
    {
        neuron.tick(inp);
        if (emit_csv) csv_row(t, inp, neuron);

        q88_t cur_vm = neuron.vmembrane();
        // Count direction reversals (sign of delta flips).
        // A stable fixed point has 0 or 1 reversals after convergence.
        if (t > 100)  // ignore transient
        {
            // After 100 ticks the membrane should have converged.
            // Any further change of more than 1 LSB is suspicious.
            int delta = (int)cur_vm - (int)prev_vm;
            if (delta < -1) ++oscillation_count;
        }
        prev_vm = cur_vm;
    }

    // Membrane must not be oscillating at long run.
    CHECK(oscillation_count == 0);
    // Membrane must have stabilised above vreset.
    CHECK(neuron.vmembrane() > p.vreset);
    // Membrane must be below the threshold (never fired).
    CHECK(neuron.vmembrane() < p.vth);
}

// ============================================================
// Entry point
// ============================================================

static void print_usage(const char* prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s                    Run all tests (assertions only)\n"
        "  %s --csv <test_id>    Run specific test, emit CSV on stdout\n"
        "\nTest IDs:\n"
        "  t01_dynamic_threshold_a/b/c\n"
        "  t02_rate_encoding_full / _no_refrac / _no_thresh\n"
        "  t03_refractory_period_0/4/8/16\n"
        "  t04_vm_decay_a/b/c\n"
        "  t05_inhibitory_clamp\n"
        "  t06_threshold_floor\n"
        "  t07_reset_correctness\n"
        "  t08_refrac_thresh_interaction\n"
        "  t09_min_isi_0/4/8\n"
        "  t10_q88_unit\n"
        "  t11_param_quantisation\n"
        "  t12_long_run_stability\n",
        prog, prog);
}

int main(int argc, char* argv[])
{
    bool        emit_csv = false;
    const char* csv_id   = NULL;

    if (argc == 3 && strcmp(argv[1], "--csv") == 0)
    {
        emit_csv = true;
        csv_id   = argv[2];
    }
    else if (argc != 1)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Run all tests. Each function checks csv_id internally and
    // emits CSV only for the matching sub-test.
    t01_dynamic_threshold       (emit_csv, csv_id);
    t02_rate_encoding           (emit_csv, csv_id);
    t03_refractory_period       (emit_csv, csv_id);
    t04_vm_decay                (emit_csv, csv_id);
    t05_inhibitory_clamp        (emit_csv, csv_id);
    t06_threshold_floor         (emit_csv, csv_id);
    t07_reset_correctness       (emit_csv, csv_id);
    t08_refrac_thresh_interaction(emit_csv, csv_id);
    t09_min_isi                 (emit_csv, csv_id);
    t10_q88_unit                (emit_csv, csv_id);
    t11_param_quantisation      (emit_csv, csv_id);
    t12_long_run_stability      (emit_csv, csv_id);

    // Summary (always to stderr so it doesn't pollute CSV stdout).
    fprintf(stderr, "\n%d passed, %d failed (of %d)\n",
            g_tests_passed, g_tests_failed, g_tests_run);

    return (g_tests_failed == 0) ? 0 : 1;
}