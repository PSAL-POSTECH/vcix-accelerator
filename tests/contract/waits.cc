// Test model: a push becomes an item five ticks later; a pop is taken at once and waits inside for an
// item, its result time unknown until then. The third form reads what a pop wrote.
#include <cinttypes>
#include <cstdio>
#include <deque>
#include <set>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr Cycle TICKS = 5;

class Waits : public Model {
 public:
  const char *name() const override { return "waits"; }
  std::vector<Encoding> owns() const override { return {{0x5B, 0x7F, "custom-2"}}; }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &, Cycle) const override { return true; }
  Cycle issue(const Insn &insn, Id id, Cycle now) override {
    if (is_pop(insn)) {
      printf("[model] issue pop\n");
      fflush(stdout);
      waiting_.push_back(id);
      return Unknown;
    }
    if (is_use(insn)) {
      printf("[model] issue use, %zu pops waiting, the last pop ready %" PRIu64 " cycles ago\n", waiting_.size(),
             now - ready_at_);
    } else {
      printf("[model] issue push, %zu pops waiting\n", waiting_.size());
      making_.push_back(TICKS);
    }
    fflush(stdout);
    return 1;
  }
  void commit(const Insn &insn, Id id, Cycle now) override {
    if (!is_pop(insn)) return;
    done_.erase(id);
    if (replaying()) return;
    printf("[model] commit pop, ready %" PRIu64 " cycles ago\n", now - ready_at_);
    fflush(stdout);
  }
  void tick(Cycle now) override {
    if (!making_.empty() && --making_.front() == 0) {
      making_.pop_front();
      items_++;
    }
    if (!waiting_.empty() && items_) {
      items_--;
      done_.insert(waiting_.front());
      waiting_.pop_front();
      ready_at_ = now;
    }
  }
  bool ready(Id id, Cycle) const override { return done_.count(id) != 0; }

 private:
  // sf.vc.v.x writes vd and reads no vector; sf.vc.xv reads vs2 and writes none.
  static bool is_pop(const Insn &insn) { return ((insn.bits >> 25) & 1) == 0; }
  static bool is_use(const Insn &insn) { return ((insn.bits >> 28) & 0xF) == 2; }

  std::deque<Cycle> making_;  // ticks left of each push
  unsigned items_ = 0;
  std::deque<Id> waiting_;  // pops with no item yet, oldest first
  std::set<Id> done_;       // pops that have their item and are not committed
  Cycle ready_at_ = 0;      // the cycle the last pop got its item
};

}  // namespace

VCIX_ACCEL_REGISTER(Waits)
