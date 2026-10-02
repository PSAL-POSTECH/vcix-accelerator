// Example: the timing face of a TPU's units behind the two custom opcodes, as one model made of unit classes; each call goes to the unit that owns the instruction.
#include <cinttypes>
#include <cstdio>
#include <deque>
#include <iterator>

#include "vcix_accel.hpp"

#include "misc.hpp"
#include "sfu.hpp"
// SYSTOLIC: #include "systolic.hpp"

namespace {

using namespace vcix_accel;

// Opcode, funct3, and funct6 with vm (funct7 on custom-1); FUNCTION_FIELD adds the field at 19:15.
constexpr uint32_t FUNCTION = 0xFE00707F;
constexpr uint32_t FUNCTION_FIELD = 0xFE0FF07F;

// What the model owns: tpu::Sfu's sf.vc.v.iv, then tpu::Misc's on custom-2 (the lane number, the systolic array's compute, the cross-lane unit) and on custom-1 (the DMA).
constexpr Encoding ENCODINGS[] = {
    {0x2000305B, FUNCTION, "verf"},
    {0x2400305B, FUNCTION, "vtanh"},
    {0x2800305B, FUNCTION_FIELD, "vsin"},
    {0x2800B05B, FUNCTION_FIELD, "vcos"},
    {0x2801305B, FUNCTION_FIELD, "vlog"},
    {0x2801B05B, FUNCTION_FIELD, "vatan"},
    {0x2C00305B, FUNCTION, "vexp"},
    {0x0000305B, FUNCTION, "vlane_idx"},
    {0x0600305B, FUNCTION, "compute"},
    {0x2E00305B, FUNCTION, "xlu_push"},
    {0xAE00305B, FUNCTION, "xlu_push_pattern"},
    {0x0400305B, FUNCTION, "xlu_pop"},
    {0x0200302B, FUNCTION, "mvin2"},
    {0x0400302B, FUNCTION, "mvin"},
    {0x0600302B, FUNCTION, "mvout"},
    {0x0E00302B, FUNCTION, "dma_config_desc"},
    {0x1C00302B, FUNCTION, "mvin3"},
};

// SYSTOLIC: tpu::Systolic becomes a member, with a line in configure, can_accept, issue, commit, tick and reset.
class Tpu : public Model {
 public:
  const char *name() const override { return "tpu"; }
  // SYSTOLIC: tpu::Systolic::encodings() is appended here.
  std::vector<Encoding> owns() const override { return {std::begin(ENCODINGS), std::end(ENCODINGS)}; }

  void configure(const Config &config) override {
    trace_ = config.uint("tpu_trace", 0) != 0;
    sfu_.configure(config);
    misc_.configure(config);
  }

  // The functional face is written elsewhere.
  void execute(const Host &, const Insn &) override {}

  bool can_accept(const Insn &insn, Cycle now) const override {
    if (sfu_.owns(insn)) return sfu_.can_accept(insn, now);
    if (misc_.owns(insn)) return misc_.can_accept(insn, now);
    return false;
  }
  Cycle issue(const Insn &insn, Id id, Cycle now) override {
    if (trace_) trace_issue(insn, now);
    if (sfu_.owns(insn)) return sfu_.issue(insn, id, now);
    if (misc_.owns(insn)) return misc_.issue(insn, id, now);
    return 1;
  }
  void commit(const Insn &insn, Id id, Cycle now) override {
    if (trace_) trace_commit(insn, now);
    if (sfu_.owns(insn)) sfu_.commit(insn, id, now);
    if (misc_.owns(insn)) misc_.commit(insn, id, now);
  }
  void tick(Cycle now) override {
    sfu_.tick(now);
    misc_.tick(now);
  }
  void reset() override {
    sfu_.reset();
    misc_.reset();
    issued_any_ = false;
    last_issue_ = 0;
    in_flight_.clear();
  }

 private:
  const char *name_of(const Insn &insn) const {
    for (const Encoding &e : owns())
      if ((insn.bits & e.mask) == e.match) return e.name;
    return "?";
  }
  void trace_issue(const Insn &insn, Cycle now) {
    if (issued_any_)
      printf("[tpu] issue %s: %" PRIu64 " cycles after the last issue, %zu in flight\n", name_of(insn),
             now - last_issue_, in_flight_.size());
    else
      printf("[tpu] issue %s: the first\n", name_of(insn));
    fflush(stdout);
    issued_any_ = true;
    last_issue_ = now;
    in_flight_.push_back(now);
  }
  void trace_commit(const Insn &insn, Cycle now) {
    const Cycle issued = in_flight_.front();
    in_flight_.pop_front();
    if (replaying()) return;
    printf("[tpu] commit %s: %" PRIu64 " cycles after its issue\n", name_of(insn), now - issued);
    fflush(stdout);
  }

  tpu::Sfu sfu_;
  tpu::Misc misc_;

  bool trace_ = false;
  // Kept only under trace_; no answer to the simulator reads them.
  bool issued_any_ = false;
  Cycle last_issue_ = 0;
  // Issue cycles of what is not committed, oldest first.
  std::deque<Cycle> in_flight_;
};

}  // namespace

VCIX_ACCEL_REGISTER(Tpu)
