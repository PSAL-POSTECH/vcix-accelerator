// Test model: holds up to DEPTH instructions at once, and reports how many were in flight at every issue.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr size_t DEPTH = 4;
constexpr Cycle LATENCY = 10;

class Pipelined : public Model {
 public:
  const char *name() const override { return "pipelined"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "piped"}}; }

  void execute(const Host &, const Insn &) override {
    printf("[model] execute\n");
    fflush(stdout);
  }
  bool can_accept(const Insn &, Cycle) const override { return in_flight_ < DEPTH; }
  Cycle issue(const Insn &, Id, Cycle now) override {
    printf("[model] issue cycle=%" PRIu64 " pending=%zu\n", now, in_flight_);
    fflush(stdout);
    in_flight_++;
    return LATENCY;
  }
  void commit(const Insn &, Id, Cycle now) override {
    in_flight_--;
    if (replaying()) return;
    printf("[model] commit cycle=%" PRIu64 "\n", now);
    fflush(stdout);
  }

 private:
  size_t in_flight_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Pipelined)
