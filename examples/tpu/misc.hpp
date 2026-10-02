// The tpu model's instructions with no timing of their own: one cycle each.
#ifndef TPU_MISC_HPP
#define TPU_MISC_HPP

#include <iterator>
#include <vector>

#include "vcix_accel.hpp"

namespace tpu {

class Misc {
 public:
  static std::vector<vcix_accel::Encoding> encodings() { return {std::begin(ENCODINGS), std::end(ENCODINGS)}; }
  bool owns(const vcix_accel::Insn &insn) const {
    for (const vcix_accel::Encoding &e : ENCODINGS)
      if ((insn.bits & e.mask) == e.match) return true;
    return false;
  }
  void configure(const vcix_accel::Config &) {}
  bool can_accept(const vcix_accel::Insn &, vcix_accel::Cycle) const { return true; }
  vcix_accel::Cycle issue(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle) { return 1; }
  void commit(const vcix_accel::Insn &, vcix_accel::Id, vcix_accel::Cycle) {}
  void tick(vcix_accel::Cycle) {}
  void reset() {}

 private:
  // custom-2: lane number, compute, the multi-precision array until it has a unit of its own. custom-1: DMA.
  static constexpr uint32_t FUNCTION = 0xFE00707F;
  static constexpr vcix_accel::Encoding ENCODINGS[] = {
      {0x0000305B, FUNCTION, "vlane_idx"},
      {0x0600305B, FUNCTION, "compute"},
      {0x2A00305B, FUNCTION, "msa push"},
      {0x0C00305B, FUNCTION, "msa pop"},
      {0x0200302B, FUNCTION, "mvin2"},
      {0x0400302B, FUNCTION, "mvin"},
      {0x0600302B, FUNCTION, "mvout"},
      {0x0E00302B, FUNCTION, "dma_config_desc"},
      {0x1C00302B, FUNCTION, "mvin3"},
  };
};

}  // namespace tpu

#endif
