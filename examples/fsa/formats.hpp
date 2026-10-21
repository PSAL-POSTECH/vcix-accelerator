// FSA's two formats (VCA-EPFL/FSA 3d2c70b, FSA4X4Fp16Config): halves into the multipliers, singles in the adders and
// the accumulator; a tile is N x N of them, N the array's rows, columns and head dimension.
#ifndef FSA_FORMATS_HPP
#define FSA_FORMATS_HPP

#include <cmath>
#include <cstdint>
#include <vector>

#include "easyfloat.hpp"

namespace fsa {

using easyfloat::Float;
using Matrix = std::vector<std::vector<Float>>;
constexpr int MUL_EW = 5, MUL_MW = 10, ACC_EW = 8, ACC_MW = 23;

inline Float acc_of(double x) { return easyfloat::round_to(Float::of_double(x), ACC_EW, ACC_MW); }
inline Float mul_of(double x) { return easyfloat::round_to(Float::of_double(x), MUL_EW, MUL_MW); }
inline Matrix matrix(size_t rows, size_t cols, const Float &x) { return Matrix(rows, std::vector<Float>(cols, x)); }

// a - b in a's format, as FPCmpUnit computes it: a * 1 + (-b).
inline Float sub(const Float &a, const Float &b) {
  return easyfloat::fma(a, easyfloat::round_to(Float::of_double(1.0), a.ew, a.mw), easyfloat::neg(b), a.ew, a.mw);
}

}  // namespace fsa

#endif
