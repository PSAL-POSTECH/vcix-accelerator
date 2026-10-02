// The tpu model's systolic array: input queue, delay line of width + height - 1, output queue.
#ifndef TPU_SYSTOLIC_HPP
#define TPU_SYSTOLIC_HPP

#include <string>
#include <vector>

#include "vcix_accel.hpp"

namespace tpu {

class Systolic {
 public:
  using Config = vcix_accel::Config;
  using Cycle = vcix_accel::Cycle;
  using Encoding = vcix_accel::Encoding;
  using Id = vcix_accel::Id;
  using Insn = vcix_accel::Insn;

  static std::vector<Encoding> encodings() {
    return {{INPUT_PUSH, FORM, "systolic input push"},
            {WEIGHT_PUSH, FORM, "systolic weight push"},
            {POP, FORM, "systolic pop"}};
  }
  bool owns(const Insn &insn) const { return is(insn, INPUT_PUSH) || is(insn, WEIGHT_PUSH) || is(insn, POP); }

  void configure(const Config &config) {
    const uint64_t lanes = config.uint(LANES_KEY, 128);
    const uint64_t entries = config.uint(QUEUE_KEY, 256);
    if (lanes < 1 || lanes > MAX_LANES)
      throw vcix_accel::ConfigError(LANES_KEY, config.get(LANES_KEY), "is not a lane count the array can have");
    if (entries < 1 || entries > UINT32_MAX)
      throw vcix_accel::ConfigError(QUEUE_KEY, config.get(QUEUE_KEY), "is not a queue size");
    slots_ = static_cast<uint32_t>(2 * lanes - 1);
    capacity_ = static_cast<uint32_t>(entries);
    line_.assign((slots_ + 63) / 64, 0);
    head_ = 0;
  }

  bool can_accept(const Insn &insn, Cycle now) const {
    if (now < free_at_) return false;
    if (is(insn, INPUT_PUSH)) return insn.vl <= capacity_ - input_;
    if (is(insn, POP)) return insn.vl <= output_;
    return true;
  }
  Cycle issue(const Insn &insn, Id, Cycle now) {
    free_at_ = now + 1;
    if (is(insn, INPUT_PUSH)) input_ += insn.vl;
    if (is(insn, POP)) output_ -= insn.vl;
    return 1;
  }
  void commit(const Insn &, Id, Cycle) {}
  // One slot leaves and is refilled, unless the output queue is full.
  void tick(Cycle) {
    if (output_ == capacity_) return;
    uint64_t &word = line_[head_ / 64];
    const uint64_t slot = uint64_t{1} << (head_ % 64);
    if (word & slot) output_++;
    if (input_) {
      word |= slot;
      input_--;
    } else {
      word &= ~slot;
    }
    head_ = head_ + 1 == slots_ ? 0 : head_ + 1;
  }
  void reset() {
    input_ = output_ = 0;
    line_.assign(line_.size(), 0);
    head_ = 0;
    free_at_ = 0;
  }

  uint32_t input_entries() const { return input_; }
  uint32_t output_entries() const { return output_; }
  uint32_t slots() const { return slots_; }
  uint32_t queue_capacity() const { return capacity_; }

 private:
  // funct6, vm and funct3 of custom-2: sf.vc.iv 0, sf.vc.iv 1 and sf.vc.v.i 2.
  static constexpr uint32_t FORM = 0xFE00707F;
  static constexpr uint32_t INPUT_PUSH = 0x2200305B;
  static constexpr uint32_t WEIGHT_PUSH = 0x2600305B;
  static constexpr uint32_t POP = 0x0800305B;
  static constexpr uint64_t MAX_LANES = 1u << 20;
  static constexpr const char *LANES_KEY = "vpu_num_lanes";
  static constexpr const char *QUEUE_KEY = "tpu_systolic_queue_entries";

  static bool is(const Insn &insn, uint32_t match) { return (insn.bits & FORM) == match; }

  uint32_t slots_ = 255;
  uint32_t capacity_ = 256;
  uint32_t input_ = 0;
  uint32_t output_ = 0;
  // The slot that leaves next.
  uint32_t head_ = 0;
  // One bit per slot: set for data, clear for a bubble.
  std::vector<uint64_t> line_ = std::vector<uint64_t>(4);
  // The first cycle the unit takes another instruction.
  Cycle free_at_ = 0;
};

}  // namespace tpu

#endif
