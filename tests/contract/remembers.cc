// Test model: an instance remembers how many commits it has seen, and its latency says so.
// It takes one instruction at a time and reports each configure, issue and commit.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr Cycle LATENCY = 10;

class Remembers : public Model {
 public:
  const char *name() const override { return "remembers"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void configure(const Config &) override { report("configure"); }
  void execute(const Host &, const Insn &) override { report("execute"); }
  bool can_accept(const Insn &, Cycle, const Pending &pending) const override { return pending.empty(); }
  Cycle latency(const Insn &, Cycle, const Pending &) const override {
    report("issue");
    return LATENCY + commits_;
  }
  void commit(const Insn &, Cycle) override {
    commits_++;
    report("commit");
  }
  void reset() override { commits_ = 0; }

 private:
  void report(const char *entry) const {
    printf("[model] %s, commits seen %" PRIu64 "\n", entry, commits_);
    fflush(stdout);
  }
  uint64_t commits_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Remembers)
