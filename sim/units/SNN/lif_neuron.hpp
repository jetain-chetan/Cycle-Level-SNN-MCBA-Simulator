#ifndef LIF_NEURON_HPP
#define LIF_NEURON_HPP

//============================================================
// Leaky Integrate-and-Fire (LIF) Neuron Model
// Fixed-Point Q8.8 (16-bit signed)
//============================================================
// Models a single LIF neuron with:
//   - Dynamic (adaptive) threshold
//   - Refractory period
//   - Q8.8 fixed-point arithmetic throughout
//
// This class is a pure state machine. It has no concept of
// simulated time, no ClockDomain, and no dependency on the
// GlobalClock scheduler. The caller (an SNN layer or unit)
// is responsible for deciding when to call tick().
//
// One call to tick() corresponds to one rising clock edge
// delivered to the neuron — equivalent to one posedge in
// the Verilog reference (testRefrac.v).
//
// Reference Verilog: lif_neuron_fixed (Q6.7, 13-bit)
// This implementation: Q8.8, 16-bit — scale factor = 256.
//
// Parameter scaling (256 = 1.0):
//   VTH        = 1.00  ->  256
//   VRESET     = 0.00  ->    0
//   alphaLEAK  = 0.95  ->  243
//   alphaTH    = 0.98  ->  251
//   deltaTH    = 1.25  ->  320
//
// Dependency chain:
//   lif_neuron.hpp -> q88.hpp -> <stdint.h>
//
// Compiler: GCC 6.3.0, -std=c++14
//============================================================

#include "../../q88.hpp"  // q88_t, q88_mul, q88_from_int

// ----------------------------------------------------------
// NeuronParams — all programmable parameters for one neuron.
// Construct explicitly; there are no defaults.
//
// All voltage fields are Q8.8. refrac_period is a plain
// timestep count stored in uint8_t (max 255 timesteps).
// ----------------------------------------------------------
struct NeuronParams
{
    q88_t   vth;           // Base threshold voltage. Also the minimum
                           // value the dynamic threshold may decay to.
    q88_t   vreset;        // Membrane reset / rest potential.
                           // Must be >= 0 (hardware constraint).
    q88_t   alpha_leak;    // Membrane leak multiplier per timestep.
                           // Q8.8 fraction, e.g. 0.95 -> 243.
    q88_t   alpha_th;      // Threshold decay multiplier per timestep.
                           // Q8.8 fraction, e.g. 0.98 -> 251.
    q88_t   delta_th;      // Threshold boost multiplier applied once
                           // immediately after a spike.
                           // Q8.8, may be > 1.0, e.g. 1.25 -> 320.
    uint8_t refrac_period; // Refractory window in timesteps.
                           // Counter counts down from this value.
};

// ----------------------------------------------------------
// LIFNeuron — single LIF neuron state machine.
// ----------------------------------------------------------
class LIFNeuron
{
public:
    // Construct with fully-specified parameters.
    // Calls reset() internally so the neuron starts in a
    // well-defined state identical to hardware power-on.
    explicit LIFNeuron(const NeuronParams& params)
        : params_(params)
        , vmembrane_(params.vreset)
        , vthreshold_(params.vth)
        , refractory_ctr_(0)
        , spike_out_(false)
    {}

    // Reset to power-on state. Models the rst_n path in the Verilog.
    // Safe to call at any point before or between tick() calls.
    void reset()
    {
        vmembrane_      = params_.vreset;
        vthreshold_     = params_.vth;
        refractory_ctr_ = 0;
        spike_out_      = false;
    }

    // ----------------------------------------------------------
    // tick() — advance the neuron by one timestep.
    //
    // syn_input: Q8.8 synaptic current for this timestep.
    //            Corresponds to one element of the MCBA result
    //            vector. May be negative (inhibitory input).
    //
    // Returns: spike_out — true if the neuron fired this step.
    //
    // State update sequence mirrors the Verilog always block:
    //
    //  1. Compute next-state candidates (Q8.8 multiply).
    //  2. Clamp candidates to legal ranges.
    //  3. Select branch: refractory / spike / integrate.
    //  4. Write state registers.
    //
    // The spike comparison (vm_next >= vthreshold_) uses the
    // CURRENT vthreshold_ register, i.e. the value from the
    // end of the previous timestep — matching the Verilog
    // register semantics exactly.
    // ----------------------------------------------------------
    bool tick(q88_t syn_input)
    {
        // ---- Step 1: compute next-state candidates -----------
        //
        // Membrane: apply leak to (Vm + synaptic input).
        // Threshold decay and boost are independent of Vm.
        //
        // All multiplies use q88_mul which performs the
        // Q16.16 -> Q8.8 rescaling via a logical (unsigned)
        // right shift. See q88.hpp for the shift rationale.
        //
        // vm_sum may be negative if syn_input is strongly
        // inhibitory. It is clamped in Step 2 before being
        // stored, so q88_mul never receives a negative result
        // that would flow through to the shift.

        q88_t vm_sum = static_cast<q88_t>(vmembrane_ + syn_input);

        // Clamp vm_sum to [vreset, max] before the multiply so
        // q88_mul's precondition (non-negative input product)
        // is satisfied when alpha_leak is positive.
        // Minimum is vreset, not zero, matching the hardware
        // behaviour: the membrane cannot go below rest.
        if (vm_sum < params_.vreset)
        {
            vm_sum = params_.vreset;
        }

        q88_t vm_next = q88_mul(vm_sum,       params_.alpha_leak);
        q88_t vt_next = q88_mul(vthreshold_,  params_.alpha_th);
        q88_t vb_next = q88_mul(vthreshold_,  params_.delta_th);

        // ---- Step 2: clamp candidates -----------------------
        //
        // Membrane floor: vreset (not zero — see above note).
        // Threshold floor: vth (the baseline parameter).
        //   Prevents adaptive threshold decaying below its
        //   designed operating point. Matches Verilog:
        //   if (Vt_next < VTH) Vt_next = VTH;

        if (vm_next < params_.vreset)
        {
            vm_next = params_.vreset;
        }
        if (vt_next < params_.vth)
        {
            vt_next = params_.vth;
        }

        // ---- Step 3 & 4: branch and write state -------------
        //
        // Priority order matches Verilog:
        //   refractory check  >  spike check  >  integrate
        //
        // Spike comparison: vm_next vs vthreshold_ (current
        // register, NOT vt_next). This is deliberate — the
        // threshold register updates at the same edge as Vm,
        // so the comparison uses the pre-edge value.

        if (refractory_ctr_ > 0)
        {
            // In refractory: suppress output, hold membrane at
            // rest, allow threshold to continue decaying.
            refractory_ctr_ -= 1;
            spike_out_       = false;
            vmembrane_       = params_.vreset;
            vthreshold_      = vt_next;
        }
        else if (vm_next >= vthreshold_)
        {
            // Spike: reset membrane, boost threshold, start
            // refractory counter.
            spike_out_       = true;
            vmembrane_       = params_.vreset;
            vthreshold_      = vb_next;
            refractory_ctr_  = params_.refrac_period;
        }
        else
        {
            // Sub-threshold integration: update normally.
            spike_out_       = false;
            vmembrane_       = vm_next;
            vthreshold_      = vt_next;
            refractory_ctr_  = 0;
        }

        return spike_out_;
    }

    // ----------------------------------------------------------
    // Read-only accessors — for inspection, logging, and tests.
    // Not needed by the simulation data path.
    // ----------------------------------------------------------
    bool    spike_out()       const { return spike_out_;       }
    q88_t   vmembrane()       const { return vmembrane_;       }
    q88_t   vthreshold()      const { return vthreshold_;      }
    uint8_t refractory_ctr()  const { return refractory_ctr_;  }

    // Access the full parameter set (e.g. for serialisation).
    const NeuronParams& params() const { return params_; }

private:
    // Programmable parameters — set at construction, fixed thereafter.
    NeuronParams params_;

    // State registers — updated every tick().
    q88_t   vmembrane_;       // Current membrane potential.
    q88_t   vthreshold_;      // Current (dynamic) threshold.
    uint8_t refractory_ctr_;  // Remaining refractory timesteps.
    bool    spike_out_;       // Output spike from last tick().
};

#endif // LIF_NEURON_HPP