#pragma once

// COMPILER REQUIREMENT: GCC 6.3.0 (MinGW), C++14.
// Do NOT use std::gcd, std::lcm, or any other <numeric> addition from C++17.
// Do NOT use std::optional, std::variant, std::string_view, or other C++17 types.
// All GCD/LCM are implemented manually below.

#include <cstdint>
#include <cstdlib>    // std::abs for integral types (pre-C++17 safe)
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Integer math utilities — manual implementations required for GCC 6.3 / C++14.
// std::gcd and std::lcm were not added to <numeric> until C++17.
// ---------------------------------------------------------------------------

inline int64_t gcd64(int64_t a, int64_t b) {
    a = (a < 0) ? -a : a;
    b = (b < 0) ? -b : b;
    while (b) { int64_t t = b; b = a % b; a = t; }
    return a;
}

inline int64_t lcm64(int64_t a, int64_t b) {
    // Divide before multiplying to reduce overflow risk.
    return (a / gcd64(a, b)) * b;
}

// ---------------------------------------------------------------------------
// Ratio — exact rational number (numerator / denominator), always reduced.
//
// Used to represent clock periods without floating-point error.
// e.g. 30 MHz -> period = Ratio{100, 3} ns  (never 33.333...)
//
// All arithmetic keeps values in lowest terms via GCD reduction.
// Denominator is always positive; sign lives in the numerator.
// ---------------------------------------------------------------------------

struct Ratio {
    int64_t num;   // numerator
    int64_t den;   // denominator (always > 0)

    // Construction
    explicit Ratio(int64_t n = 0, int64_t d = 1) : num(n), den(d) {
        if (den == 0)
            throw std::invalid_argument("Ratio: denominator cannot be zero");
        reduce();
    }

    // Build from a frequency in MHz -> period in ns
    // period_ns = 1000 / freq_mhz  (since 1 MHz = 1 cycle/µs = 1000 ns/cycle)
    static Ratio from_freq_mhz(int64_t freq_mhz_num, int64_t freq_mhz_den = 1) {
        // period_ns = 1000 * freq_mhz_den / freq_mhz_num
        return Ratio(1000LL * freq_mhz_den, freq_mhz_num);
    }

    // Reduce to lowest terms, denominator positive
    void reduce() {
        if (den < 0) { num = -num; den = -den; }
        int64_t g = gcd64(num, den);   // gcd64 handles negatives internally
        num /= g;
        den /= g;
    }

    // Comparison
    bool operator==(const Ratio& o) const { return num * o.den == o.num * den; }
    bool operator!=(const Ratio& o) const { return !(*this == o); }
    bool operator< (const Ratio& o) const { return num * o.den <  o.num * den; }
    bool operator<=(const Ratio& o) const { return num * o.den <= o.num * den; }
    bool operator> (const Ratio& o) const { return o < *this; }
    bool operator>=(const Ratio& o) const { return o <= *this; }

    // Arithmetic
    Ratio operator+(const Ratio& o) const {
        return Ratio(num * o.den + o.num * den, den * o.den);
    }
    Ratio operator-(const Ratio& o) const {
        return Ratio(num * o.den - o.num * den, den * o.den);
    }
    Ratio operator*(const Ratio& o) const {
        return Ratio(num * o.num, den * o.den);
    }
    Ratio operator/(const Ratio& o) const {
        return Ratio(num * o.den, den * o.num);
    }

    // Scalar helpers
    Ratio operator*(int64_t s) const { return Ratio(num * s, den); }
    Ratio operator/(int64_t s) const { return Ratio(num, den * s); }

    // True only if this ratio is a whole number
    bool is_integer() const { return den == 1; }

    // Returns the integer value; throws if not exact
    int64_t to_integer() const {
        if (!is_integer())
            throw std::runtime_error(
                "Ratio::to_integer(): " + to_string() + " is not an integer");
        return num;
    }

    double to_double() const { return static_cast<double>(num) / den; }

    std::string to_string() const {
        if (den == 1) return std::to_string(num);
        return std::to_string(num) + "/" + std::to_string(den);
    }
};