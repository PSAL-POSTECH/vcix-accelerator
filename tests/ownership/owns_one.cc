// Test model: owns exactly one encoding (sf.vc.x with funct6 = 1) and reports
// each call with the entry point it came through.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

class OwnsOne : public Model {
 public:
  const char *name() const override { return "owns_one"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void execute(const Host &, const Insn &insn) override { report("execute", insn); }
  bool can_accept(const Insn &, Cycle, const Pending &) const override { return true; }
  Cycle latency(const Insn &insn, Cycle, const Pending &) const override {
    report("issue", insn);
    return 1;
  }
  void commit(const Insn &insn, Cycle) override { report("commit", insn); }

 private:
  void report(const char *entry, const Insn &insn) const {
    printf("[model] %s %08" PRIx32 "\n", entry, insn.bits);
    fflush(stdout);
  }
};

}  // namespace

VCIX_ACCEL_REGISTER(OwnsOne)
