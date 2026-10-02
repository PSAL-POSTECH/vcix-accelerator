// Test model: defines a function both simulators export (softfloat's f16_to_f32), reports
// which one its own call reached, and allocates and frees memory.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "vcix_accel.hpp"

extern "C" __attribute__((noinline)) uint32_t f16_to_f32(uint16_t) { return 0x0badc0de; }

namespace {

using namespace vcix_accel;

class OwnSymbols : public Model {
 public:
  const char *name() const override { return "own_symbols"; }
  std::vector<Encoding> owns() const override { return {{0x5B, 0x7F, "vcix"}}; }

  void configure(const Config &) override {
    printf("[model] f16_to_f32(0x3c00) = 0x%08x\n", f16_to_f32(0x3c00));
    fflush(stdout);
    allocate();
  }
  void execute(const Host &, const Insn &) override { allocate(); }
  bool can_accept(const Insn &, Cycle, const Pending &) const override { return true; }
  Cycle latency(const Insn &, Cycle, const Pending &) const override { return 1; }
  void commit(const Insn &, Cycle) override { allocate(); }

 private:
  void allocate() {
    void *block = realloc(malloc(4096), 1 << 20);
    free(block);
    char *copy = strdup("allocated inside libc");
    free(copy);
    int *array = new int[1000];
    delete[] array;
    kept_ += std::string(100, 'x');
    std::cout << "[model] allocated, keeping " << kept_.size() << std::endl;
  }
  std::string kept_;
};

}  // namespace

VCIX_ACCEL_REGISTER(OwnSymbols)
