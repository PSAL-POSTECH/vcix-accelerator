// The instructions of the tpu model that have no timing of their own: each takes one cycle, and any number are taken in a cycle.
#ifndef TPU_MISC_HPP
#define TPU_MISC_HPP

#include "vcix_accel.hpp"

namespace tpu {

class Misc {
 public:
  bool owns(const vcix_accel::Insn &insn) const {
    if (vcix_accel::funct3(insn) != 3) return false;
    const uint32_t opcode = insn.bits & 0x7F;
    const uint32_t function = funct7(insn);
    if (opcode == CUSTOM_1)
      return function == MVIN2 || function == MVIN || function == MVOUT || function == CONFIG_DESC ||
             function == MVIN3;
    if (opcode == CUSTOM_2)
      return function == VLANE_IDX || function == XLU_POP || function == COMPUTE || function == XLU_PUSH ||
             function == XLU_PUSH_PATTERN;
    return false;
  }
  void configure(const vcix_accel::Config &) {}
  bool can_accept(const vcix_accel::Insn &, vcix_accel::Cycle) const { return true; }
  vcix_accel::Cycle issue(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle) { return 1; }
  void commit(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle) {}
  void tick(vcix_accel::Cycle) {}
  void reset() {}

 private:
  static constexpr uint32_t CUSTOM_1 = 0x2B, CUSTOM_2 = 0x5B;
  // custom-2: funct6 and vm, as the seven bits they are.
  static constexpr uint32_t VLANE_IDX = 0x00, XLU_POP = 0x02, COMPUTE = 0x03, XLU_PUSH = 0x17,
                            XLU_PUSH_PATTERN = 0x57;
  // custom-1: funct7 of the DMA's instructions.
  static constexpr uint32_t MVIN2 = 1, MVIN = 2, MVOUT = 3, CONFIG_DESC = 7, MVIN3 = 14;

  static uint32_t funct7(const vcix_accel::Insn &insn) { return insn.bits >> 25; }
};

}  // namespace tpu

#endif
