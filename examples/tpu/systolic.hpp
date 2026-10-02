// The tpu model's systolic array: input queue, delay line of width + height - 1, output queue.
#ifndef TPU_SYSTOLIC_HPP
#define TPU_SYSTOLIC_HPP

#include <string>
#include <vector>

#include "vcix_accel.hpp"

#include "stream.hpp"

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
    stream_.configure(static_cast<uint32_t>(2 * lanes - 1), static_cast<uint32_t>(entries));
  }

  bool can_accept(const Insn &insn, Cycle now) const {
    if (now < free_at_) return false;
    if (is(insn, INPUT_PUSH)) return stream_.has_room(insn.vl);
    if (is(insn, POP)) return stream_.holds(insn.vl);
    return true;
  }
  Cycle issue(const Insn &insn, Id, Cycle now) {
    free_at_ = now + 1;
    if (is(insn, INPUT_PUSH)) stream_.push(insn.vl);
    if (is(insn, POP)) stream_.pop(insn.vl);
    return 1;
  }
  void commit(const Insn &, Id, Cycle) {}
  void tick(Cycle) { stream_.tick(); }
  void reset() {
    stream_.reset();
    free_at_ = 0;
  }

  uint32_t input_entries() const { return stream_.input_entries(); }
  uint32_t output_entries() const { return stream_.output_entries(); }
  uint32_t slots() const { return stream_.slots(); }
  uint32_t queue_capacity() const { return stream_.queue_capacity(); }

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

  Stream stream_{255, 256};
  // The first cycle the unit takes another instruction.
  Cycle free_at_ = 0;
};

}  // namespace tpu

#endif
