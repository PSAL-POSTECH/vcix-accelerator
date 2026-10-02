// Test model: a queue of two commands, worked on one at a time for ten ticks each, and a wait instruction
// accepted only when the queue is empty. Each issue reports what finished and the cycles that had no tick.
#include <cinttypes>
#include <cstdio>
#include <deque>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr size_t DEPTH = 2;
constexpr Cycle TICKS = 10;

class Queued : public Model {
 public:
  const char *name() const override { return "queued"; }
  std::vector<Encoding> owns() const override {
    return {{0x0600405B, 0xFE00707F, "command"}, {0x0A00405B, 0xFE00707F, "wait"}};
  }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &insn, Cycle) const override {
    return is_wait(insn) ? queue_.empty() : queue_.size() < DEPTH;
  }
  Cycle issue(const Insn &insn, Id, Cycle now) override {
    printf("[model] issue %s, %u finished", is_wait(insn) ? "wait" : "command", finished_);
    if (finished_) printf(", the last %" PRIu64 " cycles ago", now - finished_at_);
    printf(", %" PRIu64 " cycles without a tick\n", missed_);
    fflush(stdout);
    if (!is_wait(insn)) queue_.push_back(TICKS);
    return 1;
  }
  void tick(Cycle now) override {
    if (ticked_) missed_ += now - last_ - 1;
    ticked_ = true;
    last_ = now;
    if (!queue_.empty() && --queue_.front() == 0) {
      queue_.pop_front();
      finished_++;
      finished_at_ = now;
    }
  }

 private:
  static bool is_wait(const Insn &insn) { return (insn.bits >> 26) == 2; }

  std::deque<Cycle> queue_;  // ticks left of each command, the one being worked on first
  bool ticked_ = false;
  Cycle last_ = 0;
  uint64_t missed_ = 0;  // cycles between two ticks that had none
  unsigned finished_ = 0;
  Cycle finished_at_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Queued)
