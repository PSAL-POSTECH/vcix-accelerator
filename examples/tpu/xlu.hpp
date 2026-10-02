// The tpu model's cross-lane unit: pushes stack a tile, the first pop starts its pass.
#ifndef TPU_XLU_HPP
#define TPU_XLU_HPP

#include <vector>

#include "vcix_accel.hpp"

namespace tpu {

class Xlu {
 public:
  using Config = vcix_accel::Config;
  using Cycle = vcix_accel::Cycle;
  using Encoding = vcix_accel::Encoding;
  using Id = vcix_accel::Id;
  using Insn = vcix_accel::Insn;

  static std::vector<Encoding> encodings() {
    return {{PUSH, FORM, "xlu_push"}, {PUSH_PATTERN, FORM, "xlu_push_pattern"}, {POP, FORM, "xlu_pop"}};
  }
  bool owns(const Insn &insn) const { return is(insn, PUSH) || is(insn, PUSH_PATTERN) || is(insn, POP); }

  void configure(const Config &config) {
    const char *key = config.get(XLU_LANES_KEY) ? XLU_LANES_KEY : LANES_KEY;
    const uint64_t lanes = config.uint(key, 128);
    if (lanes < 1 || lanes > MAX_LANES)
      throw vcix_accel::ConfigError(key, config.get(key), "is not a lane count the cross-lane unit can have");
    lanes_ = lanes;
  }

  bool can_accept(const Insn &insn, Cycle now) const {
    if (now < free_at_) return false;
    return !is(insn, POP) || pass_left_ == 0;
  }
  Cycle issue(const Insn &insn, Id, Cycle now) {
    free_at_ = now + 1;
    if (is(insn, POP)) {
      if (depth_ > 0) pass_left_ = pass_cycles();
      depth_ = 0;
    } else {
      depth_ += insn.vl;
      pre_rpu_ = (insn.bits >> 18) & 0x3;
      xu_ = (insn.bits >> 17) & 0x1;
      post_rpu_ = (insn.bits >> 15) & 0x3;
    }
    return 1;
  }
  void commit(const Insn &, Id, Cycle) {}
  void tick(Cycle) {
    if (pass_left_ > 0) pass_left_--;
  }
  void reset() {
    depth_ = 0;
    pre_rpu_ = xu_ = post_rpu_ = 0;
    pass_left_ = 0;
    free_at_ = 0;
  }

 private:
  // funct6, vm and funct3 of custom-2: sf.vc.iv 3, sf.vc.ivv 3 and sf.vc.v.i 1.
  static constexpr uint32_t FORM = 0xFE00707F;
  static constexpr uint32_t PUSH = 0x2E00305B;
  static constexpr uint32_t PUSH_PATTERN = 0xAE00305B;
  static constexpr uint32_t POP = 0x0400305B;
  static constexpr uint64_t MAX_LANES = 1u << 20;
  static constexpr const char *XLU_LANES_KEY = "tpu_xlu_lanes";
  static constexpr const char *LANES_KEY = "vpu_num_lanes";

  static bool is(const Insn &insn, uint32_t match) { return (insn.bits & FORM) == match; }

  Cycle rpu_cycles() const { return 2 * lanes_ + depth_; }
  Cycle xu_cycles() const { return lanes_ + depth_ - 1; }
  // The sum over the stages the last push did not name as bypass.
  Cycle pass_cycles() const {
    return (pre_rpu_ ? rpu_cycles() : 0) + (xu_ ? xu_cycles() : 0) + (post_rpu_ ? rpu_cycles() : 0);
  }

  uint64_t lanes_ = 128;
  // Elements each lane was pushed since the last pass started.
  uint64_t depth_ = 0;
  // The field at 19:15 of the last push: [4:3], [2] and [1:0].
  uint32_t pre_rpu_ = 0, xu_ = 0, post_rpu_ = 0;
  // Cycles the running pass still takes; no pop is taken until it is 0.
  Cycle pass_left_ = 0;
  // The first cycle the unit takes another instruction.
  Cycle free_at_ = 0;
};

}  // namespace tpu

#endif
