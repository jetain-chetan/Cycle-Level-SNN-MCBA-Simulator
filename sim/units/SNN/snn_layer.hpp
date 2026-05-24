#ifndef SNN_LAYER_HPP
#define SNN_LAYER_HPP

/*
 * snn_layer.hpp
 *
 * SnnLayer — a bank of N LIFNeuron instances driven by a common shaped-clock
 * tick supplied by the caller (the SNN unit's handler).
 *
 * Key design points
 * -----------------
 * - Each neuron carries its own NeuronParams (heterogeneous parameters).
 * - Parameters are loaded from a CSV file via snn_layer_loader.hpp, OR
 *   constructed programmatically via the vector<NeuronParams> constructor.
 * - tick() is allocation-free on the hot path; all vectors are sized at
 *   construction.
 * - The layer has no clock, no GlobalClock dependency, no Ratio dependency.
 *   It is a pure synchronous state machine. The caller decides when tick()
 *   fires, mirroring the shaped-clock trigger that will live in SnnUnit.
 * - spike_out() returns a const reference to an internal uint8_t vector
 *   (0 or 1 per neuron). uint8_t is used instead of bool to avoid
 *   std::vector<bool>'s bitfield specialisation.
 *
 * Compiler: GCC 6.3.0, -std=c++14
 * Dependencies: lif_neuron.hpp, snn_layer_loader.hpp, <vector>, <cstdio>
 */

#include "lif_neuron.hpp"
#include "snn_layer_loader.hpp"
#include <vector>
#include <cstdio>

class SnnLayer {
public:
    /* ------------------------------------------------------------------
     * Construction — programmatic (test / hand-built layers)
     *
     * layer_id  : position in the ring-counter round-robin (0-based).
     * param_vec : one NeuronParams per neuron; size determines layer width.
     * ------------------------------------------------------------------ */
    SnnLayer(uint8_t layer_id, const std::vector<NeuronParams>& param_vec)
        : layer_id_(layer_id)
        , size_(static_cast<uint16_t>(param_vec.size()))
    {
        neurons_.reserve(size_);
        for (uint16_t i = 0; i < size_; ++i) {
            neurons_.push_back(LIFNeuron(param_vec[i]));
        }
        spike_out_.resize(size_, 0);
    }

    /* ------------------------------------------------------------------
     * Construction — from CSV parameter file
     *
     * layer_id : position in the ring-counter round-robin.
     * filepath : path to a CSV file with one header row and one data row
     *            per neuron (see snn_layer_loader.hpp for format).
     *
     * On load failure the layer is left with zero neurons. Callers should
     * check size() > 0 after construction.
     * ------------------------------------------------------------------ */
    SnnLayer(uint8_t layer_id, const char* filepath)
        : layer_id_(layer_id)
        , size_(0)
    {
        std::vector<NeuronParams> param_vec;
        if (!load_layer_params(filepath, param_vec)) {
            fprintf(stderr,
                "[SnnLayer] Failed to load parameters from '%s'\n", filepath);
            return;
        }
        size_ = static_cast<uint16_t>(param_vec.size());
        neurons_.reserve(size_);
        for (uint16_t i = 0; i < size_; ++i) {
            neurons_.push_back(LIFNeuron(param_vec[i]));
        }
        spike_out_.resize(size_, 0);
    }

    /* ------------------------------------------------------------------
     * reset() — power-on reset for every neuron; clears spike vector.
     * Models the rst_n signal being asserted across the whole layer.
     * ------------------------------------------------------------------ */
    void reset()
    {
        for (uint16_t i = 0; i < size_; ++i) {
            neurons_[i].reset();
            spike_out_[i] = 0;
        }
    }

    /* ------------------------------------------------------------------
     * tick() — advance all neurons by one timestep.
     *
     * syn_inputs : pointer to an array of num_inputs Q8.8 values,
     *              one per neuron (index i -> neuron i).
     * num_inputs : must equal size_. A mismatch is flagged to stderr and
     *              the layer is not advanced.
     *
     * Returns a const reference to the internal spike vector (valid until
     * the next call to tick() or reset()).
     * ------------------------------------------------------------------ */
    const std::vector<uint8_t>& tick(const q88_t* syn_inputs,
                                      uint16_t     num_inputs)
    {
        if (num_inputs != size_) {
            fprintf(stderr,
                "[SnnLayer %u] tick() called with %u inputs but layer has "
                "%u neurons — tick ignored\n",
                (unsigned)layer_id_, (unsigned)num_inputs, (unsigned)size_);
            return spike_out_;
        }

        for (uint16_t i = 0; i < size_; ++i) {
            spike_out_[i] = neurons_[i].tick(syn_inputs[i]) ? 1u : 0u;
        }
        return spike_out_;
    }

    /* ------------------------------------------------------------------
     * Accessors
     * ------------------------------------------------------------------ */

    uint8_t  layer_id() const { return layer_id_; }
    uint16_t size()     const { return size_; }

    /* Spike vector from the most recent tick(). */
    const std::vector<uint8_t>& spike_out() const { return spike_out_; }

    /* Per-neuron state inspection — for logging and testing only. */
    q88_t   vmembrane     (uint16_t i) const { return neurons_[i].vmembrane(); }
    q88_t   vthreshold    (uint16_t i) const { return neurons_[i].vthreshold(); }
    uint8_t refractory_ctr(uint16_t i) const { return neurons_[i].refractory_ctr(); }

    /* Full parameter set for neuron i. */
    const NeuronParams& params(uint16_t i) const { return neurons_[i].params(); }

private:
    uint8_t                 layer_id_;
    uint16_t                size_;
    std::vector<LIFNeuron>  neurons_;
    std::vector<uint8_t>    spike_out_;
};

#endif /* SNN_LAYER_HPP */