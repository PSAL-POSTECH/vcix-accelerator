// Test model: accepts whatever is in flight and takes LATENCY cycles, so nothing
// of its own bounds how many overlap. It reports what was in flight at each call.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr Cycle LATENCY = 100;

class AlwaysAccepts : public Model {
 public:
  const char *name() const override { return "always_accepts"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &, Cycle, const Pending &pending) const override {
    report("accept", pending);
    return true;
  }
  Cycle latency(const Insn &, Cycle, const Pending &pending) const override {
    report("issue", pending);
    return LATENCY;
  }
  void commit(const Insn &, Cycle) override {
    printf("[model] commit\n");
    fflush(stdout);
  }

 private:
  void report(const char *entry, const Pending &pending) const {
    printf("[model] %s pending=%zu\n", entry, pending.size());
    fflush(stdout);
  }
};

}  // namespace

VCIX_ACCEL_REGISTER(AlwaysAccepts)
