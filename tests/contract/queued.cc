// Test model: a queue of two commands, worked on one at a time for ten ticks each, and a wait instruction
// accepted only when the queue is empty. It reports each tick and issue relative to its own last event.
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
  bool can_accept(const Insn &insn, Cycle, const Pending &pending) const override {
    if (is_wait(insn)) return queue_.empty() && pending.empty();
    return queue_.size() + pending.size() < DEPTH;
  }
  Cycle latency(const Insn &insn, Cycle now, const Pending &) const override {
    printf("[model] issue %s, %u finished", is_wait(insn) ? "wait" : "command", finished_);
    if (finished_) printf(", the last %" PRIu64 " cycles ago", now - finished_at_);
    printf("\n");
    fflush(stdout);
    return 1;
  }
  void commit(const Insn &insn, Cycle now) override {
    if (!busy_) last_ = now;
    busy_ = true;
    if (!is_wait(insn)) queue_.push_back(TICKS);
  }
  bool tick(Cycle now) override {
    if (busy_) printf("[model] tick +%" PRIu64 "\n", now - last_);
    else printf("[model] tick while not busy\n");
    fflush(stdout);
    last_ = now;
    if (!queue_.empty() && --queue_.front() == 0) {
      queue_.pop_front();
      finished_++;
      finished_at_ = now;
    }
    busy_ = !queue_.empty();
    return busy_;
  }

 private:
  static bool is_wait(const Insn &insn) { return (insn.bits >> 26) == 2; }

  std::deque<Cycle> queue_;  // ticks left of each command, the one being worked on first
  bool busy_ = false;
  Cycle last_ = 0;  // the last tick, or the commit that made it busy
  unsigned finished_ = 0;
  Cycle finished_at_ = 0;
};

}  // namespace

VCIX_ACCEL_REGISTER(Queued)
