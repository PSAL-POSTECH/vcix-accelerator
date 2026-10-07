// The tpu units' ports, read through the C table with no simulator: cases whose counts can be worked out by hand.
// A 4 x 4 array (7 slots, queues of 8), a cross-lane unit of 3 slots, a special-function unit of depth 4.
// Usage: tpu_ports libtpu.so
#include <dlfcn.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "vcix_accel.h"

namespace {

constexpr uint32_t INPUT_PUSH = 0x2200305B, WEIGHT_PUSH = 0x2600305B, POP = 0x0800305B, VEXP = 0x2C00305B;
constexpr uint32_t XLU_PUSH = 0x2E00305B, MSA_PUSH = 0x2A00305B, MSA_WEIGHT = 0x2A08305B, MSA_POP = 0x0C00305B;

const vcix_model *m;
int failed = 0;

void check(bool ok, const std::string &what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  fflush(stdout);
  failed |= !ok;
}

// One instance, driven a cycle at a time; issued instructions commit in order once their latency has passed.
class Run {
 public:
  Run() {
    vcix_config config = {nullptr, [](void *, const char *key) -> const char * {
                            static const std::map<std::string, std::string> machine = {
                                {"vpu_num_lanes", "4"},          {"tpu_systolic_queue_entries", "8"},
                                {"tpu_xlu_latency_cycles", "3"}, {"tpu_xlu_queue_entries", "8"},
                                {"tpu_sfu_latency_cycles", "4"}};
                            const auto found = machine.find(key);
                            return found == machine.end() ? nullptr : found->second.c_str();
                          }};
    char error[256] = "";
    self_ = m->create(&config, error, sizeof error);
    if (!self_) {
      fprintf(stderr, "create: %s\n", error);
      exit(1);
    }
  }
  ~Run() { m->destroy(self_); }

  void next() {
    now_++;
    m->tick(self_, now_);
    while (!flight_.empty() && flight_.front().done <= now_) {
      m->commit(self_, &flight_.front().insn, flight_.front().id, now_);
      flight_.erase(flight_.begin());
    }
  }
  bool issue(uint32_t bits, uint32_t vl) {
    const vcix_insn insn = {bits, vl, 32, 0};
    if (!m->can_accept(self_, &insn, now_)) return false;
    const vcix_cycle_t latency = m->issue(self_, &insn, ++id_, now_);
    flight_.push_back({id_, insn, now_ + latency});
    return true;
  }
  void squash_last() {
    if (flight_.empty()) {
      fprintf(stderr, "nothing in flight to squash\n");
      exit(1);
    }
    m->squash(self_, flight_.back().id, now_);
    flight_.pop_back();
  }
  // The value of a port's statistic, or of a commit count with unit "committed".
  uint64_t stat(const char *unit, const char *name, uint32_t kind) const {
    std::vector<uint64_t> values(m->num_stats(self_));
    m->read_stats(self_, values.data());
    for (size_t i = 0; i < values.size(); i++) {
      const vcix_stat *s = m->stat(self_, i);
      if (!strcmp(s->unit, unit) && !strcmp(s->name, name) && s->kind == kind) return values[i];
    }
    fprintf(stderr, "no statistic %s.%s of kind %u\n", unit, name, kind);
    exit(1);
  }
  std::vector<uint64_t> all() const {
    std::vector<uint64_t> values(m->num_stats(self_));
    m->read_stats(self_, values.data());
    return values;
  }
  void *self() const { return self_; }

 private:
  struct Flight {
    vcix_id_t id;
    vcix_insn insn;
    vcix_cycle_t done;
  };
  void *self_;
  vcix_cycle_t now_ = 0;
  vcix_id_t id_ = 0;
  std::vector<Flight> flight_;
};

std::string n(uint64_t v) { return std::to_string(v); }

// The list: four entries per port in the order the units are asked, a primary for each unit, then the commits.
void list() {
  Run run;
  const char *expected[][3] = {{"sfu", "entry", "1"},          {"systolic", "input", "1"}, {"systolic", "issue", "0"},
                               {"systolic", "weight_push", "0"}, {"systolic", "pop", "0"},   {"xlu", "input", "1"},
                               {"xlu", "issue", "0"},          {"msa", "input", "1"},      {"msa", "issue", "0"},
                               {"msa", "weight_push", "0"},     {"msa", "pop", "0"}};
  const size_t ports = sizeof expected / sizeof expected[0];
  bool ok = m->num_stats(run.self()) == 4 * ports + m->num_encodings;
  for (size_t p = 0; ok && p < ports; p++)
    for (uint32_t kind = 0; kind < 4; kind++) {
      const vcix_stat *s = m->stat(run.self(), 4 * p + kind);
      ok = ok && !strcmp(s->unit, expected[p][0]) && !strcmp(s->name, expected[p][1]) && s->kind == kind &&
           s->primary == uint32_t(expected[p][2][0] - '0');
    }
  for (size_t e = 0; ok && e < m->num_encodings; e++) {
    const vcix_stat *s = m->stat(run.self(), 4 * ports + e);
    ok = !strcmp(s->unit, "committed") && !strcmp(s->name, m->encodings[e].name) && s->kind == VCIX_STAT_COUNT;
  }
  check(ok, "11 ports, one primary per unit (sfu.entry, systolic/xlu/msa.input), then one commit count per encoding");
}

// One row through a 7-slot array: it enters in the tick after its push and is held 7 cycles.
void one_row() {
  Run run;
  run.next();
  run.issue(INPUT_PUSH, 1);
  for (int c = 2; c <= 20; c++) run.next();
  const uint64_t admitted = run.stat("systolic", "input", VCIX_STAT_ADMITTED);
  const uint64_t cycles = run.stat("systolic", "input", VCIX_STAT_CYCLES);
  const uint64_t occupancy = run.stat("systolic", "input", VCIX_STAT_OCCUPANCY);
  const uint64_t capacity = run.stat("systolic", "input", VCIX_STAT_CAPACITY);
  const uint64_t issued = run.stat("systolic", "issue", VCIX_STAT_ADMITTED);
  const uint64_t committed = run.stat("committed", "systolic input push", VCIX_STAT_COUNT);
  check(admitted == 1 && capacity == 1 && cycles == 20 && occupancy == 7 && issued == 1 && committed == 1,
        "one input row over 20 cycles: admitted " + n(admitted) + " of capacity " + n(capacity) + " x cycles " +
            n(cycles) + ", occupancy " + n(occupancy) + " (7 slots), issue " + n(issued) + ", committed " +
            n(committed) + "; expected 1, 1, 20, 7, 1, 1");
  run.issue(POP, 1);
  run.next();
  const uint64_t popped = run.stat("systolic", "pop", VCIX_STAT_ADMITTED);
  const uint64_t pop_capacity = run.stat("systolic", "pop", VCIX_STAT_CAPACITY);
  const uint64_t others = run.stat("sfu", "entry", VCIX_STAT_ADMITTED) + run.stat("xlu", "input", VCIX_STAT_ADMITTED) +
                          run.stat("msa", "input", VCIX_STAT_ADMITTED) +
                          run.stat("systolic", "weight_push", VCIX_STAT_ADMITTED);
  check(popped == 1 && pop_capacity == 8 && others == 0,
        "its pop admits 1 row at the pop port of 8 a cycle (" + n(popped) + ", " + n(pop_capacity) +
            "), and no other port admitted anything (" + n(others) + ")");
}

// Pushes and pops of two, one instruction a cycle: the array takes a row every cycle once it is full.
void continuous() {
  Run run;
  const int cycles = 4000;
  uint64_t short_by = 0;
  bool bounded = true;
  for (int c = 1; c <= cycles; c++) {
    run.next();
    if (!run.issue(POP, 2)) run.issue(INPUT_PUSH, 2);
    const uint64_t admitted = run.stat("systolic", "input", VCIX_STAT_ADMITTED);
    bounded = bounded && admitted <= uint64_t(c);
    short_by = uint64_t(c) - admitted;
  }
  const uint64_t admitted = run.stat("systolic", "input", VCIX_STAT_ADMITTED);
  const double utilization = double(admitted) / double(cycles);
  check(bounded && utilization > 0.99,
        "pushes and pops of two for " + n(cycles) + " cycles: input admitted " + n(admitted) +
            ", never above the cycles, utilization " + std::to_string(utilization) + " (" + n(short_by) +
            " rows short of capacity)");
}

// A vexp every cycle fills the special-function unit's entry exactly; xlu holds each element 3 cycles.
void sfu_and_xlu() {
  Run run;
  bool all = true;
  for (int c = 1; c <= 1000; c++) {
    run.next();
    all = run.issue(VEXP, 4) && all;
  }
  const uint64_t admitted = run.stat("sfu", "entry", VCIX_STAT_ADMITTED);
  const uint64_t cycles = run.stat("sfu", "entry", VCIX_STAT_CYCLES);
  check(all && admitted == 1000 && cycles == 1000, "a vexp each of 1000 cycles: sfu.entry admitted " + n(admitted) +
                                                       " in " + n(cycles) + " cycles, utilization 1");

  Run xlu;
  xlu.next();
  xlu.issue(XLU_PUSH, 5);
  for (int c = 2; c <= 30; c++) xlu.next();
  const uint64_t elements = xlu.stat("xlu", "input", VCIX_STAT_ADMITTED);
  const uint64_t held = xlu.stat("xlu", "input", VCIX_STAT_OCCUPANCY);
  check(elements == 5 && held == 15,
        "a push of 5 through 3 slots: xlu.input admitted " + n(elements) + ", occupancy " + n(held) + " (5 x 3)");

  Run msa;
  msa.next();
  msa.issue(MSA_WEIGHT, 4);
  msa.next();
  msa.issue(MSA_PUSH, 3);
  for (int c = 3; c <= 20; c++) msa.next();
  msa.issue(MSA_POP, 3);
  msa.next();
  check(msa.stat("msa", "weight_push", VCIX_STAT_ADMITTED) == 1 && msa.stat("msa", "input", VCIX_STAT_ADMITTED) == 3 &&
            msa.stat("msa", "pop", VCIX_STAT_ADMITTED) == 3 && msa.stat("msa", "issue", VCIX_STAT_ADMITTED) == 3,
        "msa: a weight push counts 1 at weight_push and enters nothing; an input push of 3 enters 3 rows; its pop takes 3");
}

// A push squashed behind a vexp still in flight leaves every count as a run that never issued it.
void squashed() {
  Run with, without;
  for (Run *run : {&with, &without}) {
    run->next();
    run->issue(INPUT_PUSH, 3);
    run->next();
    run->issue(VEXP, 1);
  }
  with.issue(INPUT_PUSH, 4);
  for (int c = 0; c < 3; c++) {
    with.next();
    without.next();
  }
  with.squash_last();
  for (int c = 0; c < 20; c++) {
    with.next();
    without.next();
  }
  check(with.all() == without.all(), "a push squashed 3 cycles after its issue, behind a vexp, leaves every statistic as if never issued");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s libtpu.so\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  auto entry = lib ? reinterpret_cast<const vcix_model *(*)()>(dlsym(lib, "vcix_accel_model")) : nullptr;
  m = entry ? entry() : nullptr;
  if (!m || m->abi_version != VCIX_ACCEL_ABI_VERSION || !m->num_stats) {
    fprintf(stderr, "%s: no table of ABI %u with statistics\n", argv[1], VCIX_ACCEL_ABI_VERSION);
    return 1;
  }
  list();
  one_row();
  continuous();
  sfu_and_xlu();
  squashed();
  return failed;
}
