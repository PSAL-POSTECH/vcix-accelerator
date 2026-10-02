// Example: the timing of a TPU's units as one model; each call goes to the unit that owns the instruction.
#include <cinttypes>
#include <cstdio>
#include <deque>
#include <vector>

#include "vcix_accel.hpp"

#include "functional.hpp"
#include "misc.hpp"
#include "sfu.hpp"
#include "systolic.hpp"
#include "xlu.hpp"

namespace {

using namespace vcix_accel;

class Tpu : public Model {
 public:
  const char *name() const override { return "tpu"; }
  std::vector<Encoding> owns() const override {
    std::vector<Encoding> all;
    for (const std::vector<Encoding> &unit :
         {tpu::Sfu::encodings(), tpu::Misc::encodings(), tpu::Systolic::encodings(), tpu::Xlu::encodings()})
      all.insert(all.end(), unit.begin(), unit.end());
    return all;
  }

  void configure(const Config &config) override {
    trace_ = config.uint("tpu_trace", 0) != 0;
    sfu_.configure(config);
    misc_.configure(config);
    systolic_.configure(config);
    xlu_.configure(config);
    functional_.configure(config);
  }

  void execute(const Host &host, const Insn &insn) override { functional_.execute(host, insn); }

  bool can_accept(const Insn &insn, Cycle now) const override {
    if (sfu_.owns(insn)) return sfu_.can_accept(insn, now);
    if (misc_.owns(insn)) return misc_.can_accept(insn, now);
    if (systolic_.owns(insn)) return systolic_.can_accept(insn, now);
    if (xlu_.owns(insn)) return xlu_.can_accept(insn, now);
    return false;
  }
  Cycle issue(const Insn &insn, Id id, Cycle now) override {
    if (trace_) trace_issue(insn, now);
    if (sfu_.owns(insn)) return sfu_.issue(insn, id, now);
    if (misc_.owns(insn)) return misc_.issue(insn, id, now);
    if (systolic_.owns(insn)) return systolic_.issue(insn, id, now);
    if (xlu_.owns(insn)) return xlu_.issue(insn, id, now);
    return 1;
  }
  void commit(const Insn &insn, Id id, Cycle now) override {
    if (trace_) trace_commit(insn, now);
    if (sfu_.owns(insn)) sfu_.commit(insn, id, now);
    if (misc_.owns(insn)) misc_.commit(insn, id, now);
    if (systolic_.owns(insn)) systolic_.commit(insn, id, now);
    if (xlu_.owns(insn)) xlu_.commit(insn, id, now);
  }
  void tick(Cycle now) override {
    sfu_.tick(now);
    misc_.tick(now);
    systolic_.tick(now);
    xlu_.tick(now);
  }
  void reset() override {
    sfu_.reset();
    misc_.reset();
    systolic_.reset();
    xlu_.reset();
    functional_.reset();
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
    printf("[tpu] issue %s: ", name_of(insn));
    if (issued_any_)
      printf("%" PRIu64 " cycles after the last issue, %zu in flight", now - last_issue_, in_flight_.size());
    else
      printf("the first");
    if (systolic_.owns(insn))
      printf(", input queue %u, output queue %u", systolic_.input_entries(), systolic_.output_entries());
    if (xlu_.owns(insn)) printf(", input queue %u, output queue %u", xlu_.input_entries(), xlu_.output_entries());
    printf("\n");
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
  tpu::Systolic systolic_;
  tpu::Xlu xlu_;
  tpu::Functional functional_;

  bool trace_ = false;
  // Kept only under trace_; no answer to the simulator reads them.
  bool issued_any_ = false;
  Cycle last_issue_ = 0;
  // Issue cycles of what is not committed, oldest first.
  std::deque<Cycle> in_flight_;
};

}  // namespace

VCIX_ACCEL_REGISTER(Tpu)
