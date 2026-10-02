// Test model: a table written by hand that has ready and no tick. A pop's result is ready fifteen
// cycles after its issue; the form that reads it reports how long after the pop it was issued.
#include <cinttypes>
#include <cstdio>
#include <map>

#include "vcix_accel.h"

namespace {

constexpr vcix_cycle_t CYCLES = 15;

struct State {
  std::map<vcix_id_t, vcix_cycle_t> pops;  // issued and not committed: the cycle each was issued
  vcix_cycle_t last_pop = 0;
};

// sf.vc.v.x writes vd and reads no vector; sf.vc.xv reads vs2 and writes none.
bool is_pop(const vcix_insn *insn) { return ((insn->bits >> 25) & 1) == 0; }
bool is_use(const vcix_insn *insn) { return ((insn->bits >> 28) & 0xF) == 2; }

const vcix_encoding encodings[] = {{0x5B, 0x7F, "custom-2"}};

const vcix_model table = {
    VCIX_ACCEL_ABI_VERSION,
    "ready_only",
    encodings,
    1,
    [](const vcix_config *, char *, size_t) -> void * { return new State; },
    [](void *self) { delete static_cast<State *>(self); },
    [](void *, const vcix_host *, const vcix_insn *) {},
    [](void *, const vcix_insn *, vcix_cycle_t) -> int { return 1; },
    [](void *self, const vcix_insn *insn, vcix_id_t id, vcix_cycle_t now) -> vcix_cycle_t {
      State &state = *static_cast<State *>(self);
      if (is_pop(insn)) {
        state.pops[id] = now;
        state.last_pop = now;
        return VCIX_LATENCY_UNKNOWN;
      }
      if (is_use(insn)) {
        printf("[model] issue use, %" PRIu64 " cycles after the pop\n", now - state.last_pop);
        fflush(stdout);
      }
      return 1;
    },
    [](void *self, vcix_id_t first, vcix_cycle_t) {
      State &state = *static_cast<State *>(self);
      state.pops.erase(state.pops.lower_bound(first), state.pops.end());
    },
    [](void *self, const vcix_insn *, vcix_id_t id, vcix_cycle_t) { static_cast<State *>(self)->pops.erase(id); },
    nullptr,  // tick
    [](void *self, vcix_id_t id, vcix_cycle_t now) -> int {
      const State &state = *static_cast<State *>(self);
      const auto pop = state.pops.find(id);
      return pop != state.pops.end() && now >= pop->second + CYCLES;
    },
    nullptr,  // reset
};

}  // namespace

extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) { return &table; }
