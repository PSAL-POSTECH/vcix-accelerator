// Test model: an instance remembers how many commits it has seen, and its latency says so.
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
  bool can_accept(const Insn &, Cycle) const override { return !in_flight_; }
  Cycle issue(const Insn &, Id, Cycle) override {
    report("issue");
    in_flight_ = true;
    return LATENCY + commits_;
  }
  void commit(const Insn &, Id, Cycle) override {
    in_flight_ = false;
    commits_++;
    if (!replaying()) report("commit");
  }
  void reset() override {
    in_flight_ = false;
    commits_ = 0;
  }

 private:
  void report(const char *entry) const {
    printf("[model] %s, commits seen %" PRIu64 "\n", entry, commits_);
    fflush(stdout);
  }
  bool in_flight_ = false;
  uint64_t commits_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Remembers)
