// Test model: cannot be configured, and says why with an exception that is not a
// ConfigError. Every face reports its call; none may ever be reached.
#include <cstdio>
#include <stdexcept>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

class Refuses : public Model {
 public:
  const char *name() const override { return "refuses"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void configure(const Config &) override { throw std::runtime_error("this machine has no such unit"); }
  void execute(const Host &, const Insn &) override { report("execute"); }
  bool can_accept(const Insn &, Cycle, const Pending &) const override {
    report("accept");
    return true;
  }
  Cycle latency(const Insn &, Cycle, const Pending &) const override {
    report("issue");
    return 1;
  }
  void commit(const Insn &, Cycle) override { report("commit"); }
  void reset() override { report("reset"); }

 private:
  void report(const char *entry) const {
    printf("[model] %s\n", entry);
    fflush(stdout);
  }
};

}  // namespace

VCIX_ACCEL_REGISTER(Refuses)
