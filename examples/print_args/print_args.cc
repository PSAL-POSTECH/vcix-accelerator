// Example: a unit that only observes. Claims all of custom-2 and custom-1 and prints what
// each face is given; computes nothing. Latency per register of the operand group
// comes from the machine description. It takes one instruction at a time, and after a
// commit it is busy for RECOVERY cycles.
#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr uint32_t MATCH_CUSTOM_2 = 0x5B;
constexpr uint32_t MATCH_CUSTOM_1 = 0x2B;
constexpr uint32_t MASK_OPCODE = 0x7F;
constexpr Cycle RECOVERY = 3;

void print_fields(const char *who, const Insn &insn) {
  printf("[%s] insn=%08" PRIx32 " funct6=%02x vm=%u f24_20=%-2u f19_15=%-2u funct3=%u f11_7=%-2u | vl=%u sew=%u lmul=2^%d",
         who, insn.bits, insn.bits >> 26, (insn.bits >> 25) & 1, rs2(insn), rs1(insn), funct3(insn), rd(insn), insn.vl,
         insn.sew_bits, insn.lmul_log2);
}

class PrintArgs : public Model {
 public:
  const char *name() const override { return "print_args"; }
  std::vector<Encoding> owns() const override { return {{MATCH_CUSTOM_2, MASK_OPCODE, "vcix"}, {MATCH_CUSTOM_1, MASK_OPCODE, "custom1"}}; }

  void configure(const Config &config) override {
    latency_ = config.uint("print_args_latency_cycles", 5);
    const char *lanes = config.get("vpu_num_lanes");
    printf("[config ] print_args_latency_cycles=%" PRIu64 " vpu_num_lanes=%s\n", latency_, lanes ? lanes : "(unset)");
    fflush(stdout);
  }

  void execute(const Host &host, const Insn &insn) override {
    print_fields("execute", insn);
    printf(" lanes=%u", host.lanes());
    if ((insn.bits & MASK_OPCODE) == MATCH_CUSTOM_1) {
      printf(" x[%u]=0x%" PRIx64 " x[%u]=0x%" PRIx64 "\n", rs1(insn), host.xreg(rs1(insn)), rs2(insn), host.xreg(rs2(insn)));
      fflush(stdout);
      return;
    }
    if (funct3(insn) == 4) printf(" x[%u]=0x%" PRIx64, rs1(insn), host.xreg(rs1(insn)));
    if (funct3(insn) == 5) {
      uint32_t bits = static_cast<uint32_t>(host.freg_bits(rs1(insn)));
      float value;
      memcpy(&value, &bits, sizeof value);
      printf(" f[%u]=%g", rs1(insn), value);
    }
    if (insn.sew_bits == 32 && insn.vl > 0) {
      const float *vs2 = host.vreg<float>(0, rs2(insn));
      printf(" v%u[0..1]=%g,%g", rs2(insn), vs2[0], insn.vl > 1 ? vs2[1] : 0.0f);
    }
    printf("\n");
    fflush(stdout);
  }

  bool can_accept(const Insn &, Cycle now, const Pending &pending) const override {
    return pending.empty() && now >= busy_until_;
  }
  Cycle latency(const Insn &insn, Cycle now, const Pending &) const override {
    Cycle cycles = latency_ * group_size(insn);
    print_fields("issue  ", insn);
    printf(" | cycle=%" PRIu64 " ready=%" PRIu64 "\n", now, now + cycles);
    fflush(stdout);
    return cycles;
  }
  void commit(const Insn &insn, Cycle now) override {
    print_fields("commit ", insn);
    printf(" | cycle=%" PRIu64 "\n", now);
    fflush(stdout);
    busy_until_ = now + RECOVERY;
  }
  void reset() override { busy_until_ = 0; }

 private:
  Cycle latency_ = 5;
  Cycle busy_until_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(PrintArgs)
