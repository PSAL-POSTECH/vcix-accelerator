// The special-function unit of the tpu model: erf, tanh, sin, cos, log, atan and exp of a vector; one instruction enters per cycle, its result ready a fixed number of cycles later.
#ifndef TPU_SFU_HPP
#define TPU_SFU_HPP

#include "vcix_accel.hpp"

namespace tpu {

class Sfu {
 public:
  bool owns(const vcix_accel::Insn &insn) const {
    if ((insn.bits & 0x7F) != CUSTOM_2 || vcix_accel::funct3(insn) != OPIVI || vm(insn) != 0) return false;
    const uint32_t function = funct6(insn);
    return function == ERF || function == TANH || function == EXP ||
           (function == SIN_COS_LOG_ATAN && vcix_accel::rs1(insn) <= ATAN);
  }
  void configure(const vcix_accel::Config &config) {
    latency_ = config.uint(LATENCY_KEY, 10);
    if (latency_ == 0) throw vcix_accel::ConfigError(LATENCY_KEY, "0", "is not a pipeline depth: at least 1");
  }
  bool can_accept(const vcix_accel::Insn &, vcix_accel::Cycle now) const { return !entered_ || now > last_entry_; }
  vcix_accel::Cycle issue(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle now) {
    entered_ = true;
    last_entry_ = now;
    return latency_;
  }
  void commit(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle) {}
  void tick(vcix_accel::Cycle) {}
  void reset() {
    entered_ = false;
    last_entry_ = 0;
  }

 private:
  static constexpr uint32_t CUSTOM_2 = 0x5B;
  static constexpr uint32_t OPIVI = 3;
  // funct6 of sf.vc.v.iv; the last of the four field values under SIN_COS_LOG_ATAN is ATAN.
  static constexpr uint32_t ERF = 0x8, TANH = 0x9, SIN_COS_LOG_ATAN = 0xA, EXP = 0xB;
  static constexpr uint32_t ATAN = 3;
  static constexpr const char *LATENCY_KEY = "tpu_sfu_latency_cycles";

  static uint32_t funct6(const vcix_accel::Insn &insn) { return insn.bits >> 26; }
  static uint32_t vm(const vcix_accel::Insn &insn) { return (insn.bits >> 25) & 1; }

  vcix_accel::Cycle latency_ = 10;
  bool entered_ = false;
  vcix_accel::Cycle last_entry_ = 0;
};

}  // namespace tpu

#endif
