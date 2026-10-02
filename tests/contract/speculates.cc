// Test model: its state counts what it was told, and each issue reports that state.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

class Speculates : public Model {
 public:
  const char *name() const override { return "speculates"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &, Cycle) const override { return true; }
  Cycle issue(const Insn &, Id, Cycle now) override {
    printf("[model] issue: %u issued, %u committed, %" PRIu64 " ticks missing\n", issued_, committed_,
           ticks_ ? now - first_tick_ + 1 - ticks_ : 0);
    fflush(stdout);
    issued_++;
    return 3;
  }
  void commit(const Insn &, Id, Cycle) override { committed_++; }
  void tick(Cycle now) override {
    if (!ticks_) first_tick_ = now;
    ticks_++;
  }

 private:
  unsigned issued_ = 0;
  unsigned committed_ = 0;
  Cycle first_tick_ = 0;
  uint64_t ticks_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Speculates)
