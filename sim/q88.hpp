#ifndef Q88_HPP
#define Q88_HPP

//============================================================
// Q8.8 Fixed-Point Arithmetic Helper
//============================================================
// Signed fixed-point format: 16 bits total
//   Bit 15       : sign
//   Bits 14..8   : integer part (7 bits magnitude + sign)
//   Bits  7..0   : fractional part
//
// Scale factor   : 2^8 = 256  (1.0 is represented as 256)
// Range          : -128.0 to +127.99609375
// Resolution     : 1/256 ≈ 0.00390625
//
// Dependency     : <stdint.h> only — no C++ standard library.
// Compiler       : GCC 6.3.0, -std=c++14
//
// NOTE ON RIGHT SHIFT:
//   q88_mul uses uint32_t for the shift to guarantee defined
//   behaviour under C++14. This is safe because by design the
//   multiplication result is never expected to be negative
//   before the shift (synaptic inputs that would drive Vm
//   negative are clamped by the caller before being used
//   further). The reinterpret via union preserves the bit
//   pattern so the signed int16_t result is correct.
//============================================================

#include <stdint.h>

// ----------------------------------------------------------
// Primary type alias
// ----------------------------------------------------------
typedef int16_t q88_t;

// Fractional bit count — used in shift operations.
static const int Q88_FRAC_BITS = 8;

// ----------------------------------------------------------
// Conversion helpers  (for setup / display only —
// never call these on the hot simulation path)
// ----------------------------------------------------------

// Convert a double to Q8.8.
// Clamps silently to int16_t range; does NOT round.
inline q88_t q88_from_double(double v)
{
    double scaled = v * 256.0;
    if (scaled >  32767.0) return  32767;
    if (scaled < -32768.0) return -32768;
    return static_cast<q88_t>(static_cast<int16_t>(scaled));
}

// Convert Q8.8 to double (display / debug only).
inline double q88_to_double(q88_t v)
{
    return static_cast<double>(v) / 256.0;
}

// Convert a plain integer to Q8.8  (e.g. q88_from_int(1) == 256).
inline q88_t q88_from_int(int v)
{
    return static_cast<q88_t>(static_cast<int16_t>(v << Q88_FRAC_BITS));
}

// ----------------------------------------------------------
// Core arithmetic
// ----------------------------------------------------------

// Q8.8 multiply: (a * b) in Q8.8.
//
// Method:
//   1. Widen both operands to int32_t and multiply — product is Q16.16.
//   2. Reinterpret the int32_t product as uint32_t to perform a
//      well-defined logical right shift by 8 bits, yielding Q8.8 in
//      the lower 16 bits.
//   3. Reinterpret the lower 16 bits back as int16_t (two's complement
//      bit pattern is preserved; the cast is safe on all GCC targets).
//
// Precondition (enforced by the neuron's clamp logic):
//   The caller guarantees that the Q16.16 product is >= 0 before the
//   shift. Negative intermediate products are a design error at the
//   call site, not handled here.
//
// Overflow:
//   If the true result exceeds Q8.8 range the upper bits are silently
//   truncated — consistent with Verilog's [Q+WIDTH-1:Q] slice.
inline q88_t q88_mul(q88_t a, q88_t b)
{
    int32_t  product   = static_cast<int32_t>(a) * static_cast<int32_t>(b);
    uint32_t u_product = static_cast<uint32_t>(product);
    uint32_t shifted   = u_product >> static_cast<uint32_t>(Q88_FRAC_BITS);
    // Isolate the lower 16 bits and reinterpret as signed.
    uint16_t lower16   = static_cast<uint16_t>(shifted & 0xFFFFu);
    q88_t    result;
    // Use memcpy-style bit reinterpretation via union — defined in C++14.
    // (A direct cast from uint16_t to int16_t is implementation-defined;
    //  the union route is the portable C++14 idiom for type-punning.)
    union { uint16_t u; int16_t s; } pun;
    pun.u  = lower16;
    result = pun.s;
    return result;
}

#endif // Q88_HPP