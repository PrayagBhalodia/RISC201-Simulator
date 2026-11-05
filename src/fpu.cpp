// fpu.cpp
// IEEE-754 binary32 built from integer operations: unpack, align, operate,
// normalize, round to nearest even, pack. Section 2.10 is the specification.
//
// Every arithmetic path carries kGuardBits extra bits below the significand.
// The lowest of them is sticky - once a discarded bit was non-zero it stays
// non-zero - which is what lets one rounding step at the end see the whole
// exact result it is rounding away from.

#include "fpu.h"
#include <utility>

namespace risc201 {

namespace {

constexpr uint32_t kSignMask = 0x80000000u;  // bit 31
constexpr uint32_t kExpMask = 0x7F800000u;   // bits 30:23
constexpr uint32_t kFracMask = 0x007FFFFFu;  // bits 22:0
constexpr uint32_t kQuietNan = 0x7FC00000u;  // the single NaN this FPU produces
constexpr uint32_t kIntMax = 0x7FFFFFFFu;    // FTOI saturates here, and at kIntMin
constexpr uint32_t kIntMin = 0x80000000u;
constexpr int kBias = 127;                   // excess-127 exponent bias
constexpr int kFracBits = 23;                // stored mantissa width
constexpr int kMinExp = -126;                // true exponent of the smallest normal
constexpr int kMaxExp = 127;                 // true exponent of the largest finite
constexpr int kGuardBits = 3;                // guard, round and sticky
constexpr uint64_t kSigTop = 1ull << (kFracBits + kGuardBits);  // leading bit once normalized

// A normalized value: sig always carries the leading 1 in bit kFracBits, so a
// subnormal input is normalized on the way in and every path below can assume
// a 24-bit significand. Only packing has to think about subnormals again.
struct Unpacked {
    uint32_t sign = 0;
    int exp = 0;
    uint32_t sig = 0;
    bool zero = false, inf = false, nan = false;
};

// `v` shifted right, with everything shifted out collapsed into bit 0.
uint64_t shiftRightSticky(uint64_t v, int n) {
    if (n <= 0) return v;
    if (n >= 64) return v != 0 ? 1u : 0u;
    const uint64_t lost = v & ((1ull << n) - 1u);
    return (v >> n) | (lost != 0 ? 1u : 0u);
}

Unpacked unpack(uint32_t x) {
    Unpacked u;
    u.sign = (x & kSignMask) ? 1u : 0u;
    const uint32_t biased = (x & kExpMask) >> kFracBits;
    const uint32_t frac = x & kFracMask;

    if (biased == 0xFFu) {
        u.inf = (frac == 0);
        u.nan = !u.inf;
    } else if (biased == 0) {
        if (frac == 0) {
            u.zero = true;
        } else {
            u.exp = kMinExp;
            u.sig = frac;
            while ((u.sig & (1u << kFracBits)) == 0) {
                u.sig <<= 1;
                --u.exp;
            }
        }
    } else {
        u.exp = static_cast<int>(biased) - kBias;
        u.sig = frac | (1u << kFracBits);
    }
    return u;
}

uint32_t packInf(uint32_t sign) { return (sign << 31) | kExpMask; }
uint32_t packZero(uint32_t sign) { return sign << 31; }

bool isNanBits(uint32_t x) {
    return (x & kExpMask) == kExpMask && (x & kFracMask) != 0;
}

bool isZeroBits(uint32_t x) { return (x & ~kSignMask) == 0; }

// The one place rounding happens. `sig` holds the significand with kGuardBits
// extra low bits and any leading position; this normalizes it, denormalizes it
// again if the exponent falls below the smallest normal, rounds to nearest
// even and packs the result.
uint32_t roundAndPack(uint32_t sign, int exp, uint64_t sig, FpStatus& f) {
    if (sig == 0) return packZero(sign);

    while (sig >= (kSigTop << 1)) {
        sig = shiftRightSticky(sig, 1);
        ++exp;
    }
    while (sig < kSigTop) {
        sig <<= 1;
        --exp;
    }

    // Below the smallest normal the significand gives up bits instead of the
    // exponent going any lower. That is what makes the result subnormal.
    if (exp < kMinExp) {
        sig = shiftRightSticky(sig, kMinExp - exp);
        exp = kMinExp;
    }

    const uint64_t rem = sig & ((1ull << kGuardBits) - 1u);
    const uint64_t half = 1ull << (kGuardBits - 1);
    uint64_t frac = sig >> kGuardBits;
    if (rem > half || (rem == half && (frac & 1u) != 0)) ++frac;
    if (rem != 0) f.inexact = true;

    if (frac >= (1ull << (kFracBits + 1))) {  // rounding carried into a new bit
        frac >>= 1;
        ++exp;
    }

    if (exp > kMaxExp) {
        f.overflow = true;
        f.inexact = true;
        return packInf(sign);
    }
    if (frac < (1ull << kFracBits)) {  // still subnormal after rounding
        if (f.inexact) f.underflow = true;
        return (sign << 31) | static_cast<uint32_t>(frac);
    }
    const uint32_t biased = static_cast<uint32_t>(exp + kBias);
    return (sign << 31) | (biased << kFracBits) |
           (static_cast<uint32_t>(frac) & kFracMask);
}

uint32_t fpAdd(uint32_t a, uint32_t b, FpStatus& f) {
    const Unpacked ua = unpack(a);
    const Unpacked ub = unpack(b);

    if (ua.nan || ub.nan) return kQuietNan;
    if (ua.inf || ub.inf) {
        if (ua.inf && ub.inf && ua.sign != ub.sign) {
            f.invalid = true;        // inf - inf has no value to report
            return kQuietNan;
        }
        return packInf(ua.inf ? ua.sign : ub.sign);
    }
    if (ua.zero && ub.zero) return packZero(ua.sign & ub.sign);  // only -0 + -0 is -0
    if (ua.zero) return b;
    if (ub.zero) return a;

    int ea = ua.exp, eb = ub.exp;
    uint64_t sa = static_cast<uint64_t>(ua.sig) << kGuardBits;
    uint64_t sb = static_cast<uint64_t>(ub.sig) << kGuardBits;
    uint32_t signA = ua.sign, signB = ub.sign;
    if (ea < eb) {
        std::swap(ea, eb);
        std::swap(sa, sb);
        std::swap(signA, signB);
    }
    sb = shiftRightSticky(sb, ea - eb);

    uint32_t sign;
    uint64_t sig;
    if (signA == signB) {
        sig = sa + sb;
        sign = signA;
    } else if (sa >= sb) {
        sig = sa - sb;
        sign = signA;
    } else {
        sig = sb - sa;
        sign = signB;
    }
    if (sig == 0) return packZero(0);  // exact cancellation is +0 under RNE

    return roundAndPack(sign, ea, sig, f);
}

uint32_t fpMul(uint32_t a, uint32_t b, FpStatus& f) {
    const Unpacked ua = unpack(a);
    const Unpacked ub = unpack(b);
    const uint32_t sign = ua.sign ^ ub.sign;

    if (ua.nan || ub.nan) return kQuietNan;
    if (ua.inf || ub.inf) {
        if (ua.zero || ub.zero) {
            f.invalid = true;        // inf * 0 has no value to report
            return kQuietNan;
        }
        return packInf(sign);
    }
    if (ua.zero || ub.zero) return packZero(sign);

    // Two 24-bit significands make a 47- or 48-bit product; bring it down to
    // the kGuardBits-extended form, collapsing the rest into the sticky bit.
    const uint64_t product = static_cast<uint64_t>(ua.sig) * ub.sig;
    const uint64_t sig = shiftRightSticky(product, kFracBits - kGuardBits);
    return roundAndPack(sign, ua.exp + ub.exp, sig, f);
}

uint32_t fpDiv(uint32_t a, uint32_t b, FpStatus& f) {
    const Unpacked ua = unpack(a);
    const Unpacked ub = unpack(b);
    const uint32_t sign = ua.sign ^ ub.sign;

    if (ua.nan || ub.nan) return kQuietNan;
    if ((ua.inf && ub.inf) || (ua.zero && ub.zero)) {
        f.invalid = true;            // inf/inf and 0/0 have no value to report
        return kQuietNan;
    }
    if (ua.inf) return packInf(sign);
    if (ub.inf) return packZero(sign);
    if (ub.zero) {
        f.divByZero = true;          // unlike integer DIV, this reports a result
        return packInf(sign);
    }
    if (ua.zero) return packZero(sign);

    // One extra bit above the kGuardBits-extended form absorbs a quotient that
    // lands below 1.0; the exponent below pays it back. An exact remainder
    // test gives the sticky bit, so no precision is lost before rounding.
    const uint64_t numerator = static_cast<uint64_t>(ua.sig)
                               << (kFracBits + kGuardBits + 1);
    uint64_t quotient = numerator / ub.sig;
    if (numerator % ub.sig != 0) quotient |= 1u;
    return roundAndPack(sign, ua.exp - ub.exp - 1, quotient, f);
}

// Sign-magnitude does not order as an integer, so map each value to a key that
// does: flip every bit of a negative, set the top bit of a positive.
uint32_t orderKey(uint32_t x) {
    return (x & kSignMask) ? ~x : (x | kSignMask);
}

// Comparisons raise nothing: this FPU has no signaling NaN to report.
uint32_t fpCompare(AluOp op, uint32_t a, uint32_t b) {
    if (isNanBits(a) || isNanBits(b)) return 0;  // NaN makes every relation false
    if (isZeroBits(a) && isZeroBits(b)) return op == AluOp::FEQ ? 1u : 0u;
    if (op == AluOp::FEQ) return a == b ? 1u : 0u;
    return orderKey(b) < orderKey(a) ? 1u : 0u;  // FGT
}

uint32_t intToFloat(uint32_t x, FpStatus& f) {
    if (x == 0) return packZero(0);
    const uint32_t sign = (x & kSignMask) ? 1u : 0u;
    const uint32_t mag = sign ? (0u - x) : x;  // INT32_MIN's magnitude needs unsigned

    int msb = 31;
    while ((mag & (1u << msb)) == 0) --msb;

    // Above 2^24 an int32 has more bits than the significand holds, so the
    // shift goes the other way and rounding decides the low bit.
    const int shift = (kFracBits + kGuardBits) - msb;
    const uint64_t sig = (shift >= 0)
                             ? (static_cast<uint64_t>(mag) << shift)
                             : shiftRightSticky(mag, -shift);
    return roundAndPack(sign, msb, sig, f);
}

uint32_t floatToInt(uint32_t x, FpStatus& f) {
    const Unpacked u = unpack(x);
    if (u.nan) {
        f.invalid = true;
        return kIntMax;
    }
    if (u.inf) {
        f.invalid = true;
        return u.sign ? kIntMin : kIntMax;
    }
    if (u.zero) return 0;
    if (u.exp < 0) {               // |x| < 1 truncates to zero, losing everything
        f.inexact = true;
        return 0;
    }
    if (u.exp > 31) {              // checked before shifting, which it would overflow
        f.invalid = true;
        return u.sign ? kIntMin : kIntMax;
    }

    uint64_t mag;
    if (u.exp >= kFracBits) {
        mag = static_cast<uint64_t>(u.sig) << (u.exp - kFracBits);
    } else {
        const int drop = kFracBits - u.exp;
        if ((u.sig & ((1u << drop) - 1u)) != 0) f.inexact = true;
        mag = u.sig >> drop;
    }

    // -2^31 is in range but +2^31 is not, so the limit depends on the sign.
    const uint64_t limit = u.sign ? kIntMin : kIntMax;
    if (mag > limit) {
        f.invalid = true;
        return u.sign ? kIntMin : kIntMax;
    }
    return u.sign ? (0u - static_cast<uint32_t>(mag)) : static_cast<uint32_t>(mag);
}

} // namespace

Fpu::Result Fpu::apply(AluOp op, uint32_t a, uint32_t b) const {
    Result out;
    switch (op) {
        case AluOp::FADD: out.value = fpAdd(a, b, out.raised); break;
        case AluOp::FSUB: out.value = fpAdd(a, b ^ kSignMask, out.raised); break;
        case AluOp::FMUL: out.value = fpMul(a, b, out.raised); break;
        case AluOp::FDIV: out.value = fpDiv(a, b, out.raised); break;
        case AluOp::FEQ:
        case AluOp::FGT:  out.value = fpCompare(op, a, b); break;
        case AluOp::ITOF: out.value = intToFloat(a, out.raised); break;
        case AluOp::FTOI: out.value = floatToInt(a, out.raised); break;

        // FpuEn routes these to the Alu instead, so they never arrive here;
        // they are listed so the compiler keeps checking this switch.
        case AluOp::ADD: case AluOp::SUB: case AluOp::MUL: case AluOp::DIV:
        case AluOp::MOD: case AluOp::AND: case AluOp::OR:  case AluOp::XOR:
        case AluOp::NOT: case AluOp::SHL: case AluOp::SHR: case AluOp::LUI:
        case AluOp::PASS: case AluOp::SRA:
            break;
    }
    return out;
}

} // namespace risc201
