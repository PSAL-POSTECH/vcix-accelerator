// Test model: its constructor throws, so no instance of it can exist.
#include <stdexcept>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

class CannotBeMade : public Model {
 public:
  CannotBeMade() { throw std::runtime_error("no such unit can be built"); }
  const char *name() const override { return "cannot_be_made"; }
  std::vector<Encoding> owns() const override { return {{0x0600405B, 0xFE00707F, "owned"}}; }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &, Cycle) const override { return true; }
  Cycle issue(const Insn &, Id, Cycle) override { return 1; }
  void commit(const Insn &, Id, Cycle) override {}
};

}  // namespace

VCIX_ACCEL_REGISTER(CannotBeMade)
