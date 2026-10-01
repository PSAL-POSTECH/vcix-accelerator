// Test model: a pipelined unit. It holds up to DEPTH instructions at once and each
// takes LATENCY cycles; it reports how many were in flight at every issue.
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
  bool can_accept(const Insn &, Cycle, const Pending &pending) const override { return pending.size() < DEPTH; }
  Cycle latency(const Insn &, Cycle now, const Pending &pending) const override {
    printf("[model] issue cycle=%" PRIu64 " pending=%zu\n", now, pending.size());
    fflush(stdout);
    return LATENCY;
  }
  void commit(const Insn &, Cycle now) override {
    printf("[model] commit cycle=%" PRIu64 "\n", now);
    fflush(stdout);
  }
};

}  // namespace

VCIX_ACCEL_REGISTER(Pipelined)
