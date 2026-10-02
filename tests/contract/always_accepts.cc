// Test model: accepts whatever it has in flight, and reports how many that was at each call.
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
  bool can_accept(const Insn &, Cycle) const override {
    report("accept");
    return true;
  }
  Cycle issue(const Insn &, Id, Cycle) override {
    report("issue");
    in_flight_++;
    return LATENCY;
  }
  void commit(const Insn &, Id, Cycle) override {
    in_flight_--;
    if (replaying()) return;
    printf("[model] commit\n");
    fflush(stdout);
  }

 private:
  void report(const char *entry) const {
    printf("[model] %s pending=%zu\n", entry, in_flight_);
    fflush(stdout);
  }
  size_t in_flight_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(AlwaysAccepts)
