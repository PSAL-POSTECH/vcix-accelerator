// FSA's floating point, as pyeasyfloat computes it (VCA-EPFL/easyfloat 973af49, pyeasyfloat/{float,rounding,fma,exp,
// div}.py): exact sums and products rounded once to nearest even, subnormals read and written as zero, one NaN.
// pow2() is the 8-piece linear 2^x of FSA's PEs. Each function follows its Python one step for step.
#ifndef FSA_EASYFLOAT_HPP
#define FSA_EASYFLOAT_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace easyfloat {

// An unsigned integer of any width, as Python's int is for the mantissas.
class Big {
 public:
  Big(uint64_t x = 0) {
    if (x) w_.push_back(x);
  }
  bool zero() const { return w_.empty(); }
  uint64_t bits() const { return w_.empty() ? 0 : 64 * (w_.size() - 1) + (64 - __builtin_clzll(w_.back())); }
  uint64_t low64() const { return w_.empty() ? 0 : w_[0]; }
  Big operator<<(uint64_t s) const {
    if (zero()) return *this;
    Big r;
    const uint64_t words = s / 64, k = s % 64;
    r.w_.assign(w_.size() + words + 1, 0);
    for (size_t i = 0; i < w_.size(); i++) {
      r.w_[i + words] |= w_[i] << k;
      if (k) r.w_[i + words + 1] |= w_[i] >> (64 - k);
    }
    r.trim();
    return r;
  }
  Big operator>>(uint64_t s) const {
    const uint64_t words = s / 64, k = s % 64;
    if (words >= w_.size()) return Big();
    Big r;
    r.w_.assign(w_.size() - words, 0);
    for (size_t i = 0; i < r.w_.size(); i++) {
      r.w_[i] = w_[i + words] >> k;
      if (k && i + words + 1 < w_.size()) r.w_[i] |= w_[i + words + 1] << (64 - k);
    }
    r.trim();
    return r;
  }
  // The low n bits.
  Big low(uint64_t n) const {
    Big r = *this;
    const uint64_t words = (n + 63) / 64;
    if (r.w_.size() > words) r.w_.resize(words);
    if (n % 64 && r.w_.size() == words) r.w_.back() &= (uint64_t{1} << (n % 64)) - 1;
    r.trim();
    return r;
  }
  friend Big operator+(const Big &a, const Big &b) {
    Big r;
    r.w_.assign(std::max(a.w_.size(), b.w_.size()) + 1, 0);
    unsigned __int128 carry = 0;
    for (size_t i = 0; i < r.w_.size(); i++) {
      carry += static_cast<unsigned __int128>(i < a.w_.size() ? a.w_[i] : 0) + (i < b.w_.size() ? b.w_[i] : 0);
      r.w_[i] = static_cast<uint64_t>(carry), carry >>= 64;
    }
    r.trim();
    return r;
  }
  // a - b, a >= b.
  friend Big operator-(const Big &a, const Big &b) {
    Big r = a;
    uint64_t borrow = 0;
    for (size_t i = 0; i < r.w_.size(); i++) {
      const uint64_t sub = (i < b.w_.size() ? b.w_[i] : 0), before = r.w_[i];
      r.w_[i] = before - sub - borrow;
      borrow = (before < sub) || (before - sub < borrow);
    }
    r.trim();
    return r;
  }
  friend Big operator*(const Big &a, uint64_t m) {
    Big r;
    r.w_.assign(a.w_.size() + 1, 0);
    unsigned __int128 carry = 0;
    for (size_t i = 0; i < a.w_.size(); i++) {
      carry += static_cast<unsigned __int128>(a.w_[i]) * m;
      r.w_[i] = static_cast<uint64_t>(carry), carry >>= 64;
    }
    r.w_[a.w_.size()] = static_cast<uint64_t>(carry);
    r.trim();
    return r;
  }
  friend int compare(const Big &a, const Big &b) {
    if (a.w_.size() != b.w_.size()) return a.w_.size() < b.w_.size() ? -1 : 1;
    for (size_t i = a.w_.size(); i-- > 0;)
      if (a.w_[i] != b.w_[i]) return a.w_[i] < b.w_[i] ? -1 : 1;
    return 0;
  }
  bool operator==(uint64_t x) const { return compare(*this, Big(x)) == 0; }

 private:
  void trim() {
    while (!w_.empty() && !w_.back()) w_.pop_back();
  }
  std::vector<uint64_t> w_;
};

// RawFloatPoint: sign, unbiased exponent, mantissa with its hidden bit; specials as flags.
struct Raw {
  bool sign = false;
  int64_t exp = 0;
  Big mantissa;
  bool is_zero = false, is_inf = false, is_nan = false;
};

// FloatPoint: an IEEE float of ew exponent and mw mantissa bits.
struct Float {
  int ew = 8, mw = 23;
  bool sign = false;
  uint32_t exp = 0;
  uint64_t mantissa = 0;

  int64_t bias() const { return (int64_t{1} << (ew - 1)) - 1; }
  uint32_t max_exp() const { return (uint32_t{1} << ew) - 1; }
  bool is_nan() const { return exp == max_exp() && mantissa != 0; }
  bool is_inf() const { return exp == max_exp() && mantissa == 0; }
  bool is_subnormal() const { return exp == 0 && mantissa != 0; }
  bool is_zero() const { return exp == 0 && mantissa == 0; }
  Raw raw() const {
    Raw r;
    r.sign = sign, r.exp = int64_t{exp} - bias(), r.mantissa = Big((uint64_t{1} << mw) | mantissa);
    r.is_inf = is_inf(), r.is_nan = is_nan(), r.is_zero = is_zero() || is_subnormal();
    return r;
  }
  uint64_t bits() const { return (uint64_t{sign} << (ew + mw)) | (uint64_t{exp} << mw) | mantissa; }
  static Float of_bits(uint64_t x, int ew, int mw) {
    Float f;
    f.ew = ew, f.mw = mw;
    f.mantissa = x & ((uint64_t{1} << mw) - 1), x >>= mw;
    f.exp = static_cast<uint32_t>(x & ((uint64_t{1} << ew) - 1)), x >>= ew;
    f.sign = x & 1;
    return f;
  }
  static Float of_double(double d) {
    uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    return of_bits(b, 11, 52);
  }
  float to_float() const {
    uint32_t b = static_cast<uint32_t>(bits());
    float f;
    std::memcpy(&f, &b, sizeof f);
    return f;
  }
};

// round_mantissa, to nearest even: x of xw bits to ow bits; returns the carry out.
inline bool round_mantissa(Big &x, uint64_t xw, uint64_t ow) {
  if (xw <= ow) {
    x = x << (ow - xw);
    return false;
  }
  const Big low_bits = x.low(xw - ow);
  x = x >> (xw - ow);
  const bool g = x.low64() & 1;
  bool r, sticky;
  if (xw - ow == 1) {
    sticky = false, r = !low_bits.zero();
  } else {
    sticky = !low_bits.low(xw - ow - 1).zero();
    r = !(low_bits >> (xw - ow - 1)).zero();
  }
  const bool roundup = (r && sticky) || (r && !sticky && g);
  bool carry = false;
  if (roundup) {
    x = x + Big(1);
    if (compare(x, Big(1) << ow) == 0) carry = true;
  }
  return carry;
}

inline Float round_raw(const Raw &raw, int ew, int mw) {
  Float t;
  t.ew = ew, t.mw = mw, t.sign = raw.sign;
  Big m = raw.mantissa;
  const bool carry = round_mantissa(m, m.bits(), 1 + mw);
  const int64_t biased = raw.exp + carry + t.bias();
  if (raw.is_nan) {
    t.sign = false, t.exp = t.max_exp(), t.mantissa = uint64_t{1} << (mw - 1);
  } else if (raw.is_zero || biased <= 0) {
    t.exp = 0, t.mantissa = 0;
  } else if (raw.is_inf || biased >= t.max_exp()) {
    t.exp = t.max_exp(), t.mantissa = 0;
  } else {
    t.exp = static_cast<uint32_t>(biased), t.mantissa = m.low(mw).low64();
  }
  return t;
}
inline Float round_to(const Float &x, int ew, int mw) { return round_raw(x.raw(), ew, mw); }

inline void pad_mantissa(Raw &a, Raw &b) {
  const uint64_t aw = a.mantissa.bits(), bw = b.mantissa.bits();
  if (aw < bw) a.mantissa = a.mantissa << (bw - aw);
  else if (aw > bw) b.mantissa = b.mantissa << (aw - bw);
}

inline Raw mul_unrounded(Raw a, Raw b) {
  pad_mantissa(a, b);
  if (a.exp > b.exp) std::swap(a, b);
  const uint64_t shift = static_cast<uint64_t>(b.exp - a.exp);
  const Big am = a.is_zero ? Big() : a.mantissa, bm = b.is_zero ? Big() : b.mantissa;
  Big product;
  if (!am.zero() && !bm.zero()) product = (bm << shift) * am.low64();
  const uint64_t width = a.mantissa.bits() + b.mantissa.bits() + shift;
  int64_t e = a.exp + b.exp + 1;
  if (!product.zero() && product.bits() != width) e -= 1;
  Raw r;
  r.sign = a.sign ^ b.sign;
  r.exp = (a.is_zero || b.is_zero) ? 0 : e;
  r.mantissa = product;
  r.is_zero = a.is_zero || b.is_zero;
  r.is_nan = a.is_nan || b.is_nan || (a.is_zero && b.is_inf) || (a.is_inf && b.is_zero);
  r.is_inf = a.is_inf || b.is_inf;
  return r;
}

inline Raw add_unrounded(Raw a, Raw b) {
  Raw r;
  r.is_nan = a.is_nan || b.is_nan || (a.is_inf && b.is_inf && a.sign != b.sign);
  r.is_inf = a.is_inf || b.is_inf;
  if (a.is_inf || b.is_inf) {
    r.sign = a.is_inf ? a.sign : b.sign, r.exp = 0, r.mantissa = Big(), r.is_zero = false;
    return r;
  }
  if (a.is_zero && b.is_zero) {
    r.sign = a.sign == b.sign ? a.sign : false, r.exp = 0, r.mantissa = Big(), r.is_zero = true;
    return r;
  }
  if (a.is_zero || b.is_zero) {
    const Raw &nz = a.is_zero ? b : a;
    r.sign = nz.sign, r.exp = nz.exp, r.mantissa = nz.mantissa, r.is_zero = false;
    return r;
  }
  pad_mantissa(a, b);
  if (a.exp > b.exp) std::swap(a, b);
  const uint64_t shift = static_cast<uint64_t>(b.exp - a.exp);
  const Big bs = b.mantissa << shift;
  Big sum;
  if (a.sign == b.sign) {
    sum = a.mantissa + bs, r.sign = a.sign;
  } else {
    // a.sign: bs - a; b.sign: a - bs; the sign is that of the difference.
    const Big &plus = a.sign ? bs : a.mantissa, &minus = a.sign ? a.mantissa : bs;
    const int c = compare(plus, minus);
    r.sign = c < 0;
    sum = c < 0 ? minus - plus : plus - minus;
  }
  if (sum.zero()) {
    r.is_zero = true, r.sign = false, r.exp = 0;
  } else {
    r.is_zero = false;
    r.exp = a.exp + (static_cast<int64_t>(sum.bits()) - static_cast<int64_t>(a.mantissa.bits()));
  }
  r.mantissa = sum;
  return r;
}

inline Float neg(Float x) {
  x.sign = !x.sign;
  return x;
}
// a * b + c, rounded once to (ew, mw).
inline Float fma(const Float &a, const Float &b, const Float &c, int ew, int mw) {
  return round_raw(add_unrounded(mul_unrounded(a.raw(), b.raw()), c.raw()), ew, mw);
}

// pow2_pwl(8) of exp.py: slopes and intercepts of 2^x on [-1, 0] in 8 uniform pieces, as numpy computes them in
// float64 (their bits, printed by that function).
constexpr int PIECES = 8;
constexpr uint64_t SLOPES[PIECES] = {0x3fd72b83c7d517b0ULL, 0x3fd9445cdb4659a0ULL, 0x3fdb8dccb0473120ULL,
                                     0x3fde0c3914911a60ULL, 0x3fe06236dd5b2870ULL, 0x3fe1ddd2b9819690ULL,
                                     0x3fe37bca1d0686d0ULL, 0x3fe53f391822dbc8ULL};
constexpr uint64_t INTERCEPTS[PIECES] = {0x3feb95c1e3ea8bd8ULL, 0x3fec80a0dc6c18b1ULL, 0x3fed5c2acc4c6981ULL,
                                         0x3fee23acaba38265ULL, 0x3feed1b9d52cd005ULL, 0x3fef601447bb3951ULL,
                                         0x3fefc792209c7561ULL, 0x3ff0000000000000ULL};

// split_float: x = xi + xf, |xf| < 1, and the top bits of xf's fraction that pick the piece. xi is held to 2^40 either
// way, which keeps every outcome of round_raw: an exponent past that far is past any format's range.
struct Split {
  int64_t xi = 0;
  uint64_t frac_msb = 0;
  Raw xf;
};
inline Split split_float(const Raw &x) {
  constexpr uint64_t LOOKUP = 3;
  constexpr int64_t HELD = int64_t{1} << 40;
  Split s;
  s.xf.sign = x.sign, s.xf.is_zero = x.is_zero, s.xf.is_nan = x.is_nan, s.xf.is_inf = x.is_inf;
  if (x.exp < 0) {
    s.xf.exp = x.exp, s.xf.mantissa = x.mantissa;
    const int64_t r_shift = -1 - x.exp;
    if (r_shift < static_cast<int64_t>(LOOKUP)) {
      const Big m = x.mantissa >> (x.mantissa.bits() - LOOKUP);
      s.frac_msb = (m >> static_cast<uint64_t>(r_shift)).low64();
    }
    return s;
  }
  const uint64_t shift = static_cast<uint64_t>(x.exp), mw = x.mantissa.bits();
  const Big mantissa = x.mantissa << shift;
  Big xf = mantissa.low(mw - 1);
  s.frac_msb = (xf >> (mw - 1 - LOOKUP)).low64();
  const Big xi = mantissa >> (mw - 1);
  if (xf.zero()) {
    s.xf.exp = 0, s.xf.mantissa = Big(), s.xf.is_zero = true;
  } else {
    const uint64_t lzc = mw - xf.bits();
    s.xf.exp = -static_cast<int64_t>(lzc), s.xf.mantissa = xf << lzc;
  }
  const int64_t held = xi.bits() > 40 ? HELD : static_cast<int64_t>(xi.low64());
  s.xi = x.sign ? -held : held;
  return s;
}

// 2^x for x <= 0: the piece's slope (of mul_ew, mul_mw) times xf plus its intercept (of add_ew, add_mw), unrounded,
// scaled by 2^xi and rounded once to (ew, mw).
inline Float pow2(const Float &x, int ew, int mw, int mul_ew, int mul_mw, int add_ew, int add_mw) {
  Split s = split_float(x.raw());
  const uint64_t piece = PIECES - 1 - s.frac_msb;
  const Float slope = round_to(Float::of_bits(SLOPES[piece], 11, 52), mul_ew, mul_mw);
  const Float intercept = round_to(Float::of_bits(INTERCEPTS[piece], 11, 52), add_ew, add_mw);
  const Raw m = mul_unrounded(s.xf, slope.raw());
  const Raw a = add_unrounded(m, intercept.raw());
  Raw r;
  r.sign = false, r.exp = s.xi + a.exp, r.mantissa = a.mantissa;
  r.is_zero = a.is_zero, r.is_nan = a.is_nan, r.is_inf = a.is_inf;
  return round_raw(r, ew, mw);
}

// x / y, both of one format, by restoring division with a sticky bit.
inline Float div(const Float &x, const Float &y) {
  const Raw rx = x.raw(), ry = y.raw();
  Raw r;
  r.sign = rx.sign ^ ry.sign;
  r.is_nan = rx.is_nan || ry.is_nan || (rx.is_inf && ry.is_inf);
  r.is_inf = y.is_subnormal() || (ry.is_zero && !rx.is_zero);
  r.is_zero = rx.is_zero || ry.is_inf;
  r.exp = rx.exp - ry.exp;
  const uint64_t xw = rx.mantissa.bits(), pad = xw % 2 == 0 ? 2 : 3, qbits = xw + pad;
  uint64_t rem = rx.mantissa.low64(), divisor = ry.mantissa.low64(), q = 0;
  bool first_zero = false;
  for (uint64_t i = 0; i < qbits; i++) {
    uint64_t bit = 0;
    if (rem >= divisor) rem -= divisor, bit = 1;
    else if (i == 0) first_zero = true;
    rem <<= 1;
    q = (q << 1) | bit;
  }
  if (first_zero) q <<= 1, r.exp -= 1;
  r.mantissa = Big((q << 1) | (rem != 0));
  return round_raw(r, x.ew, x.mw);
}

}  // namespace easyfloat

#endif
