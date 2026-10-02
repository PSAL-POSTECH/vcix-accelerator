// Test model, built twice (WHICH = 1 and 2): both libraries define a class with
// the same name in the global namespace, as two unrelated authors might.
#include "vcix_accel.hpp"

class Accel : public vcix_accel::Model {
 public:
  const char *name() const override { return WHICH == 1 ? "first" : "second"; }
  std::vector<vcix_accel::Encoding> owns() const override { return {{0x5Bu | (WHICH << 26), 0xFC00007Fu, "op"}}; }

  void execute(const vcix_accel::Host &, const vcix_accel::Insn &) override {}
  bool can_accept(const vcix_accel::Insn &, vcix_accel::Cycle, const vcix_accel::Pending &) const override { return true; }
  vcix_accel::Cycle latency(const vcix_accel::Insn &, vcix_accel::Cycle, const vcix_accel::Pending &) const override {
    return WHICH;
  }
  void commit(const vcix_accel::Insn &, vcix_accel::Cycle) override {}
};

VCIX_ACCEL_REGISTER(Accel)
