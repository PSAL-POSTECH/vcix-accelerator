// The functional face of the tpu model: what each instruction does to the registers and to memory, as the Spike these units came from did it (riscv-isa-sim 9f555b4, riscv/insns/torchsim_*.h).
#ifndef TPU_FUNCTIONAL_HPP
#define TPU_FUNCTIONAL_HPP

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vcix_accel.hpp"

#include "misc.hpp"
#include "sfu.hpp"
#include "systolic.hpp"

namespace tpu {

// Ends the run: an instruction the model cannot carry out has no trap to take through the interface.
[[noreturn]] inline void fail(const char *format, ...) {
  va_list arguments;
  va_start(arguments, format);
  std::fprintf(stderr, "tpu: ");
  std::vfprintf(stderr, format, arguments);
  std::fprintf(stderr, "\n");
  va_end(arguments);
  std::exit(1);
}

// IEEE half precision as SoftFloat for RISC-V converts it: round to nearest even, and every NaN is the default NaN.
inline float half_to_float(uint16_t half) {
  const uint32_t exponent = (half >> 10) & 0x1f, fraction = half & 0x3ff;
  float value;
  if (exponent == 0x1f) {
    const uint32_t bits = fraction ? 0x7fc00000u : (half & 0x8000 ? 0xff800000u : 0x7f800000u);
    std::memcpy(&value, &bits, sizeof value);
    return value;
  }
  value = std::ldexp(static_cast<float>(exponent ? fraction | 0x400 : fraction), static_cast<int>(exponent ? exponent : 1) - 25);
  return half & 0x8000 ? -value : value;
}
inline uint16_t double_to_half(double value) {
  if (std::isnan(value)) return 0x7e00;
  const uint16_t sign = std::signbit(value) ? 0x8000 : 0;
  const double magnitude = std::fabs(value);
  if (std::isinf(magnitude)) return sign | 0x7c00;
  if (magnitude == 0) return sign;
  int exponent;
  std::frexp(magnitude, &exponent);
  exponent = exponent - 1 < -14 ? -14 : exponent - 1;
  if (exponent > 15) return sign | 0x7c00;
  // In units of the last place of a half with this exponent: 1024 to 2048, or below 1024 for a subnormal.
  const double scaled = std::ldexp(magnitude, 10 - exponent);
  double rounded = std::floor(scaled);
  const double rest = scaled - rounded;
  if (rest > 0.5 || (rest == 0.5 && std::fmod(rounded, 2) == 1)) rounded += 1;
  if (rounded == 2048) {
    rounded = 1024;
    exponent++;
  }
  if (exponent > 15) return sign | 0x7c00;
  const uint16_t significand = static_cast<uint16_t>(rounded);
  if (significand < 1024) return sign | significand;
  return sign | static_cast<uint16_t>((exponent + 15) << 10) | (significand - 1024);
}
// The sum of two halves is exact in a double, so this rounds once.
inline uint16_t half_add(uint16_t a, uint16_t b) {
  return double_to_half(static_cast<double>(half_to_float(a)) + static_cast<double>(half_to_float(b)));
}

class Functional {
 public:
  using Config = vcix_accel::Config;
  using Encoding = vcix_accel::Encoding;
  using Host = vcix_accel::Host;
  using Insn = vcix_accel::Insn;

  void configure(const Config &config) {
    if (config.get(SPAD_LANE_KEY)) spad_lane_bytes_ = config.uint(SPAD_LANE_KEY, 0) * 1024;
    spad_base_ = config.hex(SPAD_BASE_KEY, 0xD0000000);
    if (const char *path = config.get(BASE_PATH_KEY)) base_path_ = path;

    table();
  }

  // Back to what configure left: the units hold nothing.
  void reset() {
    sa_input_.clear();
    sa_weight_input_.clear();
    sa_output_.clear();
    sa_weight_.clear();
    sa_ready_ = 0;
    xlu_in_.clear();
    xlu_pattern_.clear();
    xlu_out_.clear();
    xlu_depth_ = 0;
    xlu_operation_ = 0;
    dma_descriptor_ = 0;
    dma_indirect_count_ = 0;
  }

  void execute(const Host &host, const Insn &insn) {
    for (const Entry &entry : table())
      if ((insn.bits & entry.mask) == entry.match) return (this->*entry.run)(host, insn);
    fail("instruction %08x has no functional model", insn.bits);
  }

 private:
  using Handler = void (Functional::*)(const Host &, const Insn &);
  struct Entry {
    uint32_t match, mask;
    Handler run;
  };

  // What runs for each instruction. The encodings are the units'; a name no unit has is a mistake in this file.
  static const std::vector<Entry> &table() {
    static const std::vector<Entry> entries = [] {
      const std::pair<const char *, Handler> named[] = {
          {"verf", &Functional::verf},
          {"vtanh", &Functional::vtanh},
          {"vsin", &Functional::vsin},
          {"vcos", &Functional::vcos},
          {"vlog", &Functional::vlog},
          {"vatan", &Functional::vatan},
          {"vexp", &Functional::vexp},
          {"systolic input push", &Functional::systolic_input_push},
          {"systolic weight push", &Functional::systolic_weight_push},
          {"systolic pop", &Functional::systolic_pop},
          {"compute", &Functional::nothing},
          {"vlane_idx", &Functional::vlane_idx},
          {"xlu_push", &Functional::xlu_push},
          {"xlu_push_pattern", &Functional::xlu_push_pattern},
          {"xlu_pop", &Functional::xlu_pop},
          {"dma_config_desc", &Functional::dma_config_desc},
          {"mvin", &Functional::mvin},
          {"mvin2", &Functional::mvin},
          {"mvin3", &Functional::mvin},
          {"mvout", &Functional::mvout},
      };
      std::vector<Entry> made;
      for (const std::pair<const char *, Handler> &one : named) {
        const size_t before = made.size();
        for (const std::vector<Encoding> &unit : {Sfu::encodings(), Misc::encodings(), Systolic::encodings()})
          for (const Encoding &e : unit)
            if (std::string(one.first) == e.name) made.push_back({e.match, e.mask, one.second});
        if (made.size() != before + 1)
          throw std::logic_error(std::string("tpu: not exactly one unit owns an instruction named ") + one.first);
      }
      return made;
    }();
    return entries;
  }

  // Element `index` of the group that starts at vector register `reg`, as the type T.
  template <class T>
  static T get(const Host &host, uint32_t lane, uint32_t reg, uint64_t index) {
    const uint64_t per_register = host.vlen_bits() / 8 / sizeof(T);
    T value;
    std::memcpy(&value, host.vreg<unsigned char>(lane, reg + index / per_register) + index % per_register * sizeof(T),
                sizeof(T));
    return value;
  }
  template <class T>
  static void put(const Host &host, uint32_t lane, uint32_t reg, uint64_t index, T value) {
    const uint64_t per_register = host.vlen_bits() / 8 / sizeof(T);
    std::memcpy(host.vreg_mut<unsigned char>(lane, reg + index / per_register) + index % per_register * sizeof(T),
                &value, sizeof(T));
  }
  template <class T>
  static T load(const Host &host, uint64_t address) {
    T value;
    host.mem_read(address, &value, sizeof(T));
    return value;
  }
  template <class T>
  static void store(const Host &host, uint64_t address, T value) {
    host.mem_write(address, &value, sizeof(T));
  }

  void nothing(const Host &, const Insn &) {}

  // ---- The special-function unit: vd = f(vs2), element by element, in every lane.
  void special(const Host &host, const Insn &insn, const char *name, float (*single)(float), double (*wide)(double)) {
    const uint32_t vs = vcix_accel::rs2(insn), vd = vcix_accel::rd(insn);
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++) {
        if (insn.sew_bits == 16) {
          put<uint16_t>(host, lane, vd, i, double_to_half(single(half_to_float(get<uint16_t>(host, lane, vs, i)))));
        } else if (insn.sew_bits == 32) {
          put<float>(host, lane, vd, i, single(get<float>(host, lane, vs, i)));
        } else if (insn.sew_bits == 64 && wide) {
          put<double>(host, lane, vd, i, wide(get<double>(host, lane, vs, i)));
        } else {
          fail("%s: an element of %u bits is not supported", name, insn.sew_bits);
        }
      }
  }
  void verf(const Host &host, const Insn &insn) { special(host, insn, "verf", ::erff, nullptr); }
  void vtanh(const Host &host, const Insn &insn) { special(host, insn, "vtanh", ::tanhf, nullptr); }
  void vsin(const Host &host, const Insn &insn) { special(host, insn, "vsin", ::sinf, nullptr); }
  void vcos(const Host &host, const Insn &insn) { special(host, insn, "vcos", ::cosf, nullptr); }
  void vexp(const Host &host, const Insn &insn) { special(host, insn, "vexp", ::expf, nullptr); }
  void vlog(const Host &host, const Insn &insn) { special(host, insn, "vlog", ::logf, ::log); }
  void vatan(const Host &host, const Insn &insn) { special(host, insn, "vatan", ::atanf, ::atan); }

  // ---- vlane_idx: every element of vd is the number of its lane, as 64 bits whatever the element width.
  void vlane_idx(const Host &host, const Insn &insn) {
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++) put<int64_t>(host, lane, vcix_accel::rd(insn), i, lane);
  }

  // ---- The systolic array: a matrix of weights times each pushed input vector, a float per lane.
  using Queues = std::vector<std::deque<float>>;
  static float systolic_element(const Host &host, const Insn &insn, uint32_t lane, uint32_t reg, uint32_t i) {
    switch (insn.sew_bits) {
      case 8: return static_cast<float>(get<int8_t>(host, lane, reg, i));
      case 16: return half_to_float(get<uint16_t>(host, lane, reg, i));
      case 32: return get<float>(host, lane, reg, i);
      default: return 0.0f;
    }
  }
  void systolic_size(const Host &host) {
    if (sa_input_.size() == host.lanes()) return;
    sa_input_.assign(host.lanes(), {});
    sa_weight_input_.assign(host.lanes(), {});
    sa_output_.assign(host.lanes(), {});
  }
  // A weight push adds a column per pushed element; a row keeps its last `lanes` weights.
  void systolic_weight_push(const Host &host, const Insn &insn) {
    systolic_size(host);
    const uint32_t lanes = host.lanes();
    for (uint32_t lane = 0; lane < lanes; lane++)
      for (uint32_t i = 0; i < insn.vl; i++)
        sa_weight_input_[lane].push_back(systolic_element(host, insn, lane, vcix_accel::rs2(insn), i));
    if (sa_weight_.empty()) sa_weight_.assign(lanes, {});
    for (uint32_t lane = 0; lane < lanes; lane++)
      for (uint32_t i = 0; i < insn.vl; i++) {
        if (sa_weight_[lane].size() == lanes) sa_weight_[lane].pop_front();
        sa_weight_[lane].push_back(sa_weight_input_[lane].front());
        sa_weight_input_[lane].pop_front();
      }
  }
  // An input push computes at once: element i of every lane is one input vector, and lane j's output is its weights times it.
  void systolic_input_push(const Host &host, const Insn &insn) {
    systolic_size(host);
    const uint32_t lanes = host.lanes();
    for (uint32_t lane = 0; lane < lanes; lane++)
      for (uint32_t i = 0; i < insn.vl; i++)
        sa_input_[lane].push_back(systolic_element(host, insn, lane, vcix_accel::rs2(insn), i));
    if (sa_weight_.empty() || sa_weight_[0].empty()) fail("systolic input push: no weight was pushed before it");
    std::vector<float> input(lanes);
    for (uint32_t i = 0; i < insn.vl; i++) {
      for (uint32_t lane = 0; lane < lanes; lane++) {
        input[lane] = sa_input_[lane].front();
        sa_input_[lane].pop_front();
      }
      for (uint32_t lane = 0; lane < lanes; lane++) {
        float output = 0;
        uint32_t k = 0;
        for (float weight : sa_weight_[lane]) output += input[k++] * weight;
        sa_output_[lane].push_back(output);
      }
    }
    sa_ready_ += insn.vl;
  }
  void systolic_pop(const Host &host, const Insn &insn) {
    systolic_size(host);
    if (sa_ready_ < insn.vl)
      fail("systolic pop: %u elements asked, %llu computed", insn.vl, static_cast<unsigned long long>(sa_ready_));
    const uint32_t vd = vcix_accel::rd(insn);
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++) {
        if (sa_output_[lane].empty()) break;
        const float value = sa_output_[lane].front();
        sa_output_[lane].pop_front();
        switch (insn.sew_bits) {
          case 8: put<int8_t>(host, lane, vd, i, static_cast<int8_t>(value)); break;
          case 16: put<uint16_t>(host, lane, vd, i, double_to_half(value)); break;
          default: put<float>(host, lane, vd, i, value); break;
        }
      }
    sa_ready_ -= insn.vl;
  }

  // ---- The cross-lane unit: a tile is pushed a register at a time, and the first pop after a push moves it
  // across the lanes. It carries 32 raw bits per element and does no arithmetic. The field at 19:15 of the push
  // says what to do: [4:3] the lanes shuffled before, [2] depth and lane exchanged, [1:0] the lanes shuffled after.
  using Tile = std::vector<std::vector<uint32_t>>;
  enum Shuffle : uint32_t { BYPASS = 0, REPLICATE = 1, ARBITRARY = 2 };
  static uint32_t xlu_element(const Host &host, const Insn &insn, uint32_t lane, uint32_t reg, uint32_t i) {
    switch (insn.sew_bits) {
      case 8: return get<uint8_t>(host, lane, reg, i);
      case 16: return get<uint16_t>(host, lane, reg, i);
      case 32: return get<uint32_t>(host, lane, reg, i);
      default: return 0;
    }
  }
  void xlu_size(const Host &host) {
    if (xlu_in_.size() == host.lanes()) return;
    xlu_in_.assign(host.lanes(), {});
    xlu_pattern_.assign(host.lanes(), {});
    xlu_out_.assign(host.lanes(), {});
  }
  void xlu_begin_push(const Host &host, const Insn &insn) {
    xlu_size(host);
    const uint32_t operation = vcix_accel::rs1(insn);
    if (operation == 0 || (operation >> 3) > ARBITRARY || (operation & 3) > ARBITRARY)
      fail("cross-lane push: %u is not an operation of the unit", operation);
    xlu_operation_ = operation;
  }
  void xlu_push(const Host &host, const Insn &insn) {
    xlu_begin_push(host, insn);
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++)
        xlu_in_[lane].push_back(xlu_element(host, insn, lane, vcix_accel::rs2(insn), i));
    xlu_depth_ += insn.vl;
  }
  // The pattern is in the register the rd field names: a lane number of 32 bits per element.
  void xlu_push_pattern(const Host &host, const Insn &insn) {
    xlu_begin_push(host, insn);
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++) {
        xlu_in_[lane].push_back(xlu_element(host, insn, lane, vcix_accel::rs2(insn), i));
        xlu_pattern_[lane].push_back(get<uint32_t>(host, lane, vcix_accel::rd(insn), i));
      }
    xlu_depth_ += insn.vl;
  }
  // Every lane reads some lane's row: lane 0 for REPLICATE, the lane its pattern names for ARBITRARY (itself without a pattern). What is missing reads as 0.
  static Tile xlu_shuffle(uint32_t what, const Tile &tile, const Tile &pattern) {
    if (what == BYPASS) return tile;
    const size_t lanes = tile.size();
    size_t width = 0;
    for (const std::vector<uint32_t> &row : tile) width = row.size() > width ? row.size() : width;
    Tile got(lanes);
    for (size_t lane = 0; lane < lanes; lane++)
      for (size_t k = 0; k < width; k++) {
        size_t from = 0;
        if (what == ARBITRARY) from = k < pattern[lane].size() ? pattern[lane][k] : lane;
        got[lane].push_back(from < lanes && k < tile[from].size() ? tile[from][k] : 0u);
      }
    return got;
  }
  // Column k becomes lane k's row; only `depth` lanes receive anything.
  static Tile xlu_cross(const Tile &tile, uint64_t depth) {
    const size_t lanes = tile.size();
    Tile got(lanes);
    for (uint64_t k = 0; k < depth && k < lanes; k++)
      for (size_t lane = 0; lane < lanes; lane++) got[k].push_back(k < tile[lane].size() ? tile[lane][k] : 0u);
    return got;
  }
  void xlu_run() {
    const size_t lanes = xlu_in_.size();
    Tile tile(lanes), pattern(lanes);
    for (size_t lane = 0; lane < lanes; lane++) {
      for (uint64_t k = 0; k < xlu_depth_ && !xlu_in_[lane].empty(); k++) {
        tile[lane].push_back(xlu_in_[lane].front());
        xlu_in_[lane].pop_front();
      }
      pattern[lane].assign(xlu_pattern_[lane].begin(), xlu_pattern_[lane].end());
      xlu_pattern_[lane].clear();
    }
    Tile got = xlu_shuffle(xlu_operation_ >> 3, tile, pattern);
    if (xlu_operation_ & 4) got = xlu_cross(got, xlu_depth_);
    got = xlu_shuffle(xlu_operation_ & 3, got, pattern);
    // What the last pop did not take is not this tile's: it is replaced.
    for (size_t lane = 0; lane < lanes; lane++) xlu_out_[lane].assign(got[lane].begin(), got[lane].end());
    xlu_depth_ = 0;
  }
  // A lane the operation gave nothing keeps what vd held.
  void xlu_pop(const Host &host, const Insn &insn) {
    xlu_size(host);
    if (xlu_depth_ > 0) xlu_run();
    const uint32_t vd = vcix_accel::rd(insn);
    for (uint32_t lane = 0; lane < host.lanes(); lane++)
      for (uint32_t i = 0; i < insn.vl; i++) {
        if (xlu_out_[lane].empty()) break;
        const uint32_t value = xlu_out_[lane].front();
        xlu_out_[lane].pop_front();
        switch (insn.sew_bits) {
          case 8: put<uint8_t>(host, lane, vd, i, static_cast<uint8_t>(value)); break;
          case 16: put<uint16_t>(host, lane, vd, i, static_cast<uint16_t>(value)); break;
          default: put<uint32_t>(host, lane, vd, i, value); break;
        }
      }
  }

  // ---- The DMA: a tensor of up to four dimensions between memory and the scratchpad, one slice of the split
  // axis per lane. dma_config_desc names the descriptor; mvin and mvout read it when they run. x[rs1] is the
  // address in memory and x[rs2] the address in the scratchpad.
  static constexpr int INVALID_SPAD_ACCESS = 200;  // the exit code of a transfer that leaves a lane's scratchpad
  static constexpr uint64_t SKIP = ~uint64_t{0};   // not an address: another transfer owns this position

  void dma_config_desc(const Host &host, const Insn &insn) { dma_descriptor_ = host.xreg(vcix_accel::rs1(insn)); }

  struct Transfer {
    uint64_t memory, scratchpad;
    uint64_t dim_size[4], dim_low[4], dim_high[4], mm_stride[4], spad_stride[4];
    uint64_t element_size, vlane_stride;
    int split_axis;
    bool indirect, masked, accumulate, accumulate_float;
    unsigned skip_axes;
    uint64_t indirect_address, indirect_stride, indirect_element_size, indirect_lanes, fill, memory_base, memory_bytes;
    int indirect_dim;

    uint64_t lanes, outer_loops, used_lanes;
    uint64_t block_dim[4], block_stride[4], buffer_stride[4];
    uint64_t lane_stride, outer_stride, spad_outer_stride;
    std::vector<uint64_t> address;  // per position of the tensor: its address in memory, 0 for none, or SKIP
  };

  Transfer dma_transfer(const Host &host, const Insn &insn, const char *name, bool skips) const {
    if (!spad_lane_bytes_) fail("%s: the machine description has no %s", name, SPAD_LANE_KEY);
    Transfer t;
    t.memory = host.xreg(vcix_accel::rs1(insn));
    t.scratchpad = host.xreg(vcix_accel::rs2(insn));
    const uint64_t d = dma_descriptor_;
    for (int i = 0; i < 4; i++) {
      t.dim_size[i] = static_cast<uint64_t>(static_cast<int64_t>(load<int32_t>(host, d + 0 + 4 * i)));
      t.dim_low[i] = static_cast<uint64_t>(static_cast<int64_t>(load<int32_t>(host, d + 16 + 4 * i)));
      t.dim_high[i] = static_cast<uint64_t>(static_cast<int64_t>(load<int32_t>(host, d + 32 + 4 * i)));
      t.mm_stride[i] = load<uint64_t>(host, d + 48 + 8 * i);
      t.spad_stride[i] = load<uint64_t>(host, d + 80 + 8 * i);
    }
    t.element_size = load<uint16_t>(host, d + 112);
    t.vlane_stride = load<uint16_t>(host, d + 114);
    t.split_axis = load<uint8_t>(host, d + 116);
    const uint16_t flags = load<uint16_t>(host, d + 118);
    t.indirect = flags & 0x1;
    t.masked = flags & 0x2;
    t.accumulate = flags & 0x4;
    t.accumulate_float = flags & 0x8;
    t.skip_axes = skips ? (flags >> 4) & 0xF : 0;
    t.indirect_address = load<uint64_t>(host, d + 120);
    t.indirect_stride = load<uint16_t>(host, d + 128);
    t.indirect_element_size = load<uint16_t>(host, d + 130);
    t.indirect_dim = load<uint8_t>(host, d + 132);
    t.indirect_lanes = load<uint16_t>(host, d + 134);
    t.fill = load<uint64_t>(host, d + 136);
    t.memory_base = load<uint64_t>(host, d + 144);
    t.memory_bytes = load<uint64_t>(host, d + 152);

    if (t.split_axis > 3) fail("%s: the descriptor splits axis %d of 4", name, t.split_axis);
    if (t.element_size == 0 || t.vlane_stride == 0) fail("%s: the descriptor has an element size or a lane stride of 0", name);
    for (int i = 0; i < 4; i++)
      if (t.dim_size[i] == 0) fail("%s: the descriptor has a dimension of 0", name);

    t.lanes = host.lanes();
    const uint64_t *dim = t.dim_size;
    t.outer_loops = (dim[t.split_axis] + t.vlane_stride * t.lanes - 1) / (t.vlane_stride * t.lanes);
    t.used_lanes = t.outer_loops > 1 ? t.lanes : (dim[t.split_axis] + t.vlane_stride - 1) / t.vlane_stride;
    for (int i = 0; i < 4; i++) {
      t.block_dim[i] = dim[i];
      t.block_stride[i] = t.spad_stride[i];
    }
    t.block_dim[t.split_axis] = t.vlane_stride;
    for (int i = 0; i < 4; i++)
      if (t.block_stride[i] > t.spad_stride[t.split_axis])
        t.block_stride[i] = t.block_stride[i] / dim[t.split_axis] * t.vlane_stride * t.outer_loops;

    const uint64_t elements = dim[0] * dim[1] * dim[2] * dim[3];
    const uint64_t round = t.used_lanes * t.block_dim[0] * t.block_dim[1] * t.block_dim[2] * t.block_dim[3];
    const uint64_t buffer_size = (elements + round - 1) / round * round;
    t.buffer_stride[0] = dim[1] * dim[2] * dim[3];
    t.buffer_stride[1] = dim[2] * dim[3];
    t.buffer_stride[2] = dim[3];
    t.buffer_stride[3] = 1;
    t.lane_stride = t.buffer_stride[t.split_axis] * t.vlane_stride;
    t.outer_stride = t.lane_stride * t.used_lanes;
    t.spad_outer_stride = t.block_stride[t.split_axis] * t.vlane_stride;

    try {
      t.address.assign(buffer_size, 0);
    } catch (const std::bad_alloc &) {
      fail("%s: no memory for a transfer of %llu elements", name, static_cast<unsigned long long>(buffer_size));
    }
    // Outside [dim_low, dim_high) of a masked transfer there is nothing in memory: 0, or SKIP on a skip axis.
    for (uint64_t n = 0; n < dim[0]; n++)
      for (uint64_t c = 0; c < dim[1]; c++)
        for (uint64_t h = 0; h < dim[2]; h++)
          for (uint64_t w = 0; w < dim[3]; w++) {
            const uint64_t offset =
                (n * t.mm_stride[0] + c * t.mm_stride[1] + h * t.mm_stride[2] + w * t.mm_stride[3]) * t.element_size;
            // Compared as unsigned, as the bounds are kept: a negative bound is a very large one.
            const uint64_t coordinate[4] = {n, c, h, w};
            bool in_box = true;
            uint64_t outside = 0;
            for (int i = 0; i < 4; i++) {
              const bool inside = coordinate[i] >= t.dim_low[i] && coordinate[i] < t.dim_high[i];
              in_box = in_box && inside;
              if ((t.skip_axes >> i & 1) && !inside) outside = SKIP;
            }
            t.address[n * t.buffer_stride[0] + c * t.buffer_stride[1] + h * t.buffer_stride[2] + w] =
                !t.masked || in_box ? t.memory + offset : outside;
          }
    return t;
  }

  // The address the index of an indirect transfer adds to, and the index it read.
  uint64_t dma_indirect(const Host &host, const Transfer &t, const char *name, uint64_t outer, uint64_t lane,
                        const uint64_t at[4], uint64_t spad_index, uint64_t *index) const {
    uint64_t address;
    if (t.indirect_dim) {
      // One index per position along axis indirect_dim - 1, kept as a tile of its own over indirect_lanes lanes.
      const int axis = t.indirect_dim - 1;
      uint64_t coordinate = axis == 0 ? at[0] : axis == 1 ? at[1] : axis == 2 ? at[2] : at[3];
      if (axis == t.split_axis) coordinate += (outer * t.used_lanes + lane) * t.vlane_stride;
      const uint64_t lanes = t.indirect_lanes ? t.indirect_lanes : 1;
      address = t.indirect_address + coordinate / lanes * t.indirect_element_size + coordinate % lanes * spad_lane_bytes_;
    } else {
      address = t.indirect_address + spad_index * t.indirect_element_size + lane * spad_lane_bytes_;
    }
    // An index is signed.
    switch (t.indirect_element_size) {
      case 1: *index = static_cast<uint64_t>(static_cast<int64_t>(load<int8_t>(host, address))); break;
      case 2: *index = static_cast<uint64_t>(static_cast<int64_t>(load<int16_t>(host, address))); break;
      case 4: *index = static_cast<uint64_t>(static_cast<int64_t>(load<int32_t>(host, address))); break;
      case 8: *index = load<uint64_t>(host, address); break;
      default: fail("%s: an index of %llu bytes is not supported", name, static_cast<unsigned long long>(t.indirect_element_size));
    }
    return *index * t.indirect_stride * t.element_size;
  }

  // The indices an indirect transfer read, for the tools that replay it: <--base-path>/indirect_access/indirect_index<n>.raw.
  // Written as the old Spike wrote it: asking the map for a position it lacks adds that position, as 0.
  void dma_indices(const char *name, std::map<uint64_t, uint64_t> &indices) {
    if (base_path_.empty()) fail("%s: an indirect transfer writes its indices under --base-path, and none was given", name);
    const std::string path = base_path_ + "/indirect_access/indirect_index" + std::to_string(dma_indirect_count_++) + ".raw";
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file) {
      std::fprintf(stderr, "Failed to open file for writing: %s\n", path.c_str());
      return;
    }
    for (size_t i = 0; i < indices.size(); ++i) std::fwrite(&indices[i], sizeof(uint64_t), 1, file);
    std::fclose(file);
  }

  template <class T>
  static void mvin_element(const Host &host, uint64_t from, uint64_t to, bool used, uint64_t fill) {
    store<T>(host, to, used ? load<T>(host, from) : static_cast<T>(fill));
  }
  void mvin(const Host &host, const Insn &insn) {
    const Transfer t = dma_transfer(host, insn, "mvin", true);
    std::map<uint64_t, uint64_t> indices;
    for (uint64_t outer = 0; outer < t.outer_loops; outer++)
      for (uint64_t lane = 0; lane < t.lanes; lane++)
        for (uint64_t n = 0; n < t.block_dim[0]; n++)
          for (uint64_t c = 0; c < t.block_dim[1]; c++)
            for (uint64_t h = 0; h < t.block_dim[2]; h++)
              for (uint64_t w = 0; w < t.block_dim[3]; w++) {
                const uint64_t at[4] = {n, c, h, w};
                const uint64_t position = t.outer_stride * outer + t.lane_stride * lane + t.buffer_stride[0] * n +
                                          t.buffer_stride[1] * c + t.buffer_stride[2] * h + t.buffer_stride[3] * w;
                const uint64_t spad_index = t.spad_outer_stride * outer + t.block_stride[0] * n + t.block_stride[1] * c +
                                            t.block_stride[2] * h + t.block_stride[3] * w;
                bool used = lane < t.used_lanes;
                const uint64_t to = t.scratchpad + spad_index * t.element_size + lane * spad_lane_bytes_;
                uint64_t from = used ? dma_address(t, "mvin", position) : 0;
                if (from == SKIP) continue;
                used = used && from != 0;
                if (t.scratchpad + spad_index * t.element_size >= spad_base_ + spad_lane_bytes_) {
                  std::fprintf(stderr, "MVIN ERROR: Scratchpad address overflow: 0x%llx\n", static_cast<unsigned long long>(to));
                  std::exit(INVALID_SPAD_ACCESS);
                }
                if (t.indirect && used) {
                  uint64_t index;
                  from += dma_indirect(host, t, "mvin", outer, lane, at, spad_index, &index);
                  indices[position] = index;
                }
                // Outside the range the descriptor allows: nothing is read, the fill goes in.
                if (t.memory_bytes && (from < t.memory_base || from + t.element_size > t.memory_base + t.memory_bytes))
                  used = false;
                switch (t.element_size) {
                  case 1: mvin_element<uint8_t>(host, from, to, used, t.fill); break;
                  case 2: mvin_element<uint16_t>(host, from, to, used, t.fill); break;
                  case 4: mvin_element<uint32_t>(host, from, to, used, t.fill); break;
                  case 8: mvin_element<uint64_t>(host, from, to, used, t.fill); break;
                  default: break;
                }
              }
    if (t.indirect) dma_indices("mvin", indices);
  }

  void mvout(const Host &host, const Insn &insn) {
    const Transfer t = dma_transfer(host, insn, "mvout", false);
    std::map<uint64_t, uint64_t> indices;
    for (uint64_t outer = 0; outer < t.outer_loops; outer++)
      for (uint64_t lane = 0; lane < t.used_lanes; lane++)
        for (uint64_t n = 0; n < t.block_dim[0]; n++)
          for (uint64_t c = 0; c < t.block_dim[1]; c++)
            for (uint64_t h = 0; h < t.block_dim[2]; h++)
              for (uint64_t w = 0; w < t.block_dim[3]; w++) {
                const uint64_t at[4] = {n, c, h, w};
                const uint64_t position = t.outer_stride * outer + t.lane_stride * lane + t.buffer_stride[0] * n +
                                          t.buffer_stride[1] * c + t.buffer_stride[2] * h + t.buffer_stride[3] * w;
                const uint64_t spad_index = t.spad_outer_stride * outer + t.block_stride[0] * n + t.block_stride[1] * c +
                                            t.block_stride[2] * h + t.block_stride[3] * w;
                const uint64_t from = t.scratchpad + spad_index * t.element_size + lane * spad_lane_bytes_;
                uint64_t to = dma_address(t, "mvout", position);
                if (to == 0) continue;
                if (t.scratchpad + spad_index * t.element_size >= spad_base_ + spad_lane_bytes_) {
                  std::fprintf(stderr, "MVOUT ERROR: Scratchpad address overflow: 0x%llx\n", static_cast<unsigned long long>(from));
                  std::exit(INVALID_SPAD_ACCESS);
                }
                if (t.indirect) {
                  uint64_t index;
                  to += dma_indirect(host, t, "mvout", outer, lane, at, spad_index, &index);
                  indices[position] = index;
                }
                // Outside the range the descriptor allows: not this transfer's to write.
                if (t.memory_bytes && (to < t.memory_base || to + t.element_size > t.memory_base + t.memory_bytes))
                  continue;
                if (t.element_size == 1) {
                  uint8_t value = load<uint8_t>(host, from);
                  if (t.accumulate) value = static_cast<uint8_t>(load<uint8_t>(host, to) + value);
                  store<uint8_t>(host, to, value);
                } else if (t.element_size == 2) {
                  uint16_t value = load<uint16_t>(host, from);
                  if (t.accumulate) {
                    const uint16_t current = load<uint16_t>(host, to);
                    value = t.accumulate_float ? half_add(current, value) : static_cast<uint16_t>(current + value);
                  }
                  store<uint16_t>(host, to, value);
                } else if (t.element_size == 4) {
                  if (t.accumulate && t.accumulate_float)
                    store<float>(host, to, load<float>(host, to) + load<float>(host, from));
                  else
                    store<uint32_t>(host, to, (t.accumulate ? load<uint32_t>(host, to) : 0) + load<uint32_t>(host, from));
                } else if (t.element_size == 8) {
                  if (t.accumulate && t.accumulate_float)
                    store<double>(host, to, load<double>(host, to) + load<double>(host, from));
                  else
                    store<uint64_t>(host, to, (t.accumulate ? load<uint64_t>(host, to) : 0) + load<uint64_t>(host, from));
                }
              }
    if (t.indirect) dma_indices("mvout", indices);
  }
  static uint64_t dma_address(const Transfer &t, const char *name, uint64_t position) {
    if (position >= t.address.size())
      fail("%s: the descriptor reaches position %llu of a tensor of %zu", name, static_cast<unsigned long long>(position),
           t.address.size());
    return t.address[position];
  }

  static constexpr const char *SPAD_LANE_KEY = "vpu_spad_size_kb_per_lane";
  static constexpr const char *SPAD_BASE_KEY = "vpu_spad_base_vaddr";
  static constexpr const char *BASE_PATH_KEY = "run_base_path";  // the simulator's --base-path

  uint64_t spad_lane_bytes_ = 0;  // 0: the machine description does not say
  uint64_t spad_base_ = 0xD0000000;
  std::string base_path_;

  Queues sa_input_, sa_weight_input_, sa_output_;
  Queues sa_weight_;  // per lane, its row of the matrix; empty until the first weight push
  uint64_t sa_ready_ = 0;

  std::vector<std::deque<uint32_t>> xlu_in_, xlu_pattern_, xlu_out_;
  uint64_t xlu_depth_ = 0;  // elements pushed per lane since the last pop ran
  uint32_t xlu_operation_ = 0;

  uint64_t dma_descriptor_ = 0;
  uint64_t dma_indirect_count_ = 0;
};

}  // namespace tpu

#endif
