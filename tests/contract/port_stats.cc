// Checks of the statistics a model counts at its ports, with no simulator: through the C table and through Instance.
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

int failed = 0;

void expect(bool ok, const std::string &what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  fflush(stdout);
  if (!ok) failed = 1;
}

enum Variant { GOOD, NO_PRIMARY, TWO_PRIMARIES, SAME_NAME, NAMED_COMMITTED, LISTED_TWICE, CLASHING_UNITS, CLASHING_PORTS,
               CLASHING_ENCODINGS };
enum Form { OP, PUSH, POP };
constexpr uint32_t FORM_MASK = 0xF000007F;

uint32_t bits_of(Form f) { return 0x5B | (uint32_t(f) << 28); }
Form form_of(const Insn &insn) { return Form(insn.bits >> 28); }

// A pipe that takes two instructions and eight elements a cycle, and an array that is a Stream.
template <Variant V>
class Toy : public Model {
 public:
  const char *name() const override { return "toy"; }
  std::vector<Encoding> owns() const override {
    return {{bits_of(OP), FORM_MASK, "op"},
            {bits_of(PUSH), FORM_MASK, "array push"},
            {bits_of(POP), FORM_MASK, V == CLASHING_ENCODINGS ? "array_push" : "pop"}};
  }
  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &insn, Cycle now) const override {
    if (form_of(insn) == OP) return issue_.room(now) >= 1 && lanes_.room(now) >= insn.vl;
    if (form_of(insn) == PUSH) return array_.has_room(insn.vl);
    return array_.holds(insn.vl);
  }
  Cycle issue(const Insn &insn, Id, Cycle now) override {
    if (form_of(insn) == OP) {
      issue_.admit(1, now);
      lanes_.admit(insn.vl, now);
    }
    if (form_of(insn) == PUSH) array_.push(insn.vl);
    if (form_of(insn) == POP) array_.pop(insn.vl);
    return 1;
  }
  void tick(Cycle now) override { array_.tick(now); }
  void reset() override {
    issue_.reset();
    lanes_.reset();
    array_.reset();
  }
  std::vector<const Port *> ports() const override {
    if (V == LISTED_TWICE) return {&issue_, &lanes_, &array_.entry(), &lanes_};
    return {&issue_, &lanes_, &array_.entry()};
  }

 private:
  Port issue_{V == NAMED_COMMITTED ? "committed" : V == CLASHING_UNITS ? "pi.pe" : "pipe",
              V == CLASHING_PORTS ? "la.nes" : "issue", "instructions", 2,
              V == NO_PRIMARY ? Port::REPORTING : Port::PRIMARY};
  Port lanes_{V == CLASHING_UNITS ? "pi_pe" : "pipe",
              V == SAME_NAME ? "issue" : V == CLASHING_PORTS ? "la_nes" : "lanes", "elements", 8,
              V == TWO_PRIMARIES || V == CLASHING_UNITS ? Port::PRIMARY : Port::REPORTING};
  Stream array_{"array", "entry", "rows", Port::PRIMARY, 3, 4};
};

const vcix_config NO_CONFIG = {nullptr, [](void *, const char *) -> const char * { return nullptr; }};

std::vector<uint64_t> read(const vcix_model *m, void *self) {
  std::vector<uint64_t> values(m->num_stats(self));
  m->read_stats(self, values.data());
  return values;
}

// Runs a seeded program through the C table: in each cycle tick, commit what is ready, sometimes squash, issue.
struct Driver {
  const vcix_model *m = export_model<Toy<GOOD>>();
  void *self;
  std::mt19937 rng;
  std::deque<std::pair<Id, Insn>> in_flight;
  Id next = 1;
  Cycle now = 0;
  bool reads;
  std::string trace;

  Driver(unsigned seed, bool reads_between) : rng(seed), reads(reads_between) {
    char error[128] = "";
    self = m->create(&NO_CONFIG, error, sizeof error);
  }
  ~Driver() { m->destroy(self); }
  void poke() {
    if (!reads) return;
    for (int i = 0; i < 3; i++) read(m, self);
    m->stat(self, 0);
    m->num_stats(self);
  }
  Insn draw(std::mt19937 &r) {
    const Form f = Form(r() % 3);
    return {bits_of(f), uint32_t(1 + r() % (f == OP ? 8 : 3)), 32, 0};
  }
  bool try_issue(const Insn &insn) {
    poke();
    const bool ok = m->can_accept(self, &insn, now);
    trace += ok ? 'a' : 'r';
    if (!ok) return false;
    const Id id = next++;
    trace += char('0' + m->issue(self, &insn, id, now));
    in_flight.push_back({id, insn});
    return true;
  }
  // One cycle; `wrong` issues a wrong path from its own generator, which `squash_wrong` takes back later. While
  // `held`, the right path neither issues nor squashes, so the wrong path stays the youngest.
  void cycle(std::mt19937 *wrong = nullptr, bool held = false) {
    now++;
    m->tick(self, now);
    poke();
    for (int k = 0; k < 2 && !in_flight.empty() && in_flight.front().first < wrong_from; k++) {
      m->commit(self, &in_flight.front().second, in_flight.front().first, now);
      in_flight.pop_front();
    }
    const bool squash = rng() % 7 == 0;
    const Insn first_draw = draw(rng), second_draw = draw(rng);
    if (held) {
      trace += '-';
    } else if (squash && !in_flight.empty()) {
      const Id first = in_flight.back().first;
      m->squash(self, first, now);
      while (!in_flight.empty() && in_flight.back().first >= first) in_flight.pop_back();
    }
    if (!held) {
      try_issue(first_draw);
      try_issue(second_draw);
    }
    if (wrong) {
      if (wrong_from == UINT64_MAX) wrong_from = next;
      for (int k = 0; k < 3; k++) {
        const Insn insn = draw(*wrong);
        if (!m->can_accept(self, &insn, now)) continue;
        const Id id = next++;
        m->issue(self, &insn, id, now);
        in_flight.push_back({id, insn});
      }
    }
  }
  void squash_wrong() {
    if (wrong_from == UINT64_MAX) return;
    m->squash(self, wrong_from, now);
    while (!in_flight.empty() && in_flight.back().first >= wrong_from) in_flight.pop_back();
    wrong_from = UINT64_MAX;
  }
  Id wrong_from = UINT64_MAX;
};

void aborts_beyond_room() {
  int out[2];
  if (pipe(out) != 0) return expect(false, "a pipe for the child");
  const pid_t child = fork();
  if (child == 0) {
    dup2(out[1], 2);
    Port port("unit", "port", "rows", 2, Port::PRIMARY);
    port.admit(2, 5);
    port.admit(1, 5);
    _exit(0);
  }
  close(out[1]);
  char said[256] = "";
  const ssize_t n = ::read(out[0], said, sizeof said - 1);
  if (n > 0) said[n] = 0;
  int status = 0;
  waitpid(child, &status, 0);
  expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT &&
             strstr(said, "vcix_accel: port unit.port admitted 1 rows in cycle 5 with room for 0") != nullptr,
         "admitting past room() aborts, naming the port");
  Port port("unit", "port", "rows", 2, Port::PRIMARY);
  port.admit(2, 5);
  expect(port.room(5) == 0 && port.room(6) == 2 && port.admitted() == 2, "room() is the capacity again the next cycle");
}

void the_list() {
  const vcix_model *m = export_model<Toy<GOOD>>();
  char error[128] = "";
  void *self = m->create(&NO_CONFIG, error, sizeof error);
  const size_t n = m->num_stats(self);
  bool ok = n == 3 * 4 + 3 && m->stat(self, n) == nullptr;
  const char *units[] = {"pipe", "pipe", "array"}, *names[] = {"issue", "lanes", "entry"};
  const uint32_t primaries[] = {1, 0, 1};
  for (size_t p = 0; ok && p < 3; p++)
    for (uint32_t k = 0; k < 4; k++) {
      const vcix_stat *s = m->stat(self, 4 * p + k);
      ok = ok && !strcmp(s->unit, units[p]) && !strcmp(s->name, names[p]) && s->kind == k && s->primary == primaries[p];
    }
  const char *encodings[] = {"op", "array push", "pop"};
  for (size_t i = 0; ok && i < 3; i++) {
    const vcix_stat *s = m->stat(self, 12 + i);
    ok = !strcmp(s->unit, "committed") && !strcmp(s->name, encodings[i]) && s->kind == VCIX_STAT_COUNT && !s->primary;
  }
  expect(ok, "the list: per port ADMITTED, CAPACITY, CYCLES, OCCUPANCY with its primary flag, then a COUNT per encoding");
  m->destroy(self);

  const auto refused = [](const vcix_model *t, const char *why) {
    char e[128] = "";
    void *s = t->create(&NO_CONFIG, e, sizeof e);
    if (s) t->destroy(s);
    return !s && std::string(e) == why;
  };
  expect(refused(export_model<Toy<NO_PRIMARY>>(), "unit pipe has 0 primary ports, not one") &&
             refused(export_model<Toy<TWO_PRIMARIES>>(), "unit pipe has 2 primary ports, not one") &&
             refused(export_model<Toy<SAME_NAME>>(), "two ports are named pipe.issue") &&
             refused(export_model<Toy<LISTED_TWICE>>(), "two ports are named pipe.lanes") &&
             refused(export_model<Toy<NAMED_COMMITTED>>(),
                     "a port's unit is named 'committed', the name of the commit counts"),
         "create refuses a unit without one primary port, two ports of one name, one port listed twice, and a unit named "
         "committed");
  expect(refused(export_model<Toy<CLASHING_UNITS>>(), "units pi.pe and pi_pe are both pi_pe as statistics") &&
             refused(export_model<Toy<CLASHING_PORTS>>(),
                     "ports pipe.la.nes and pipe.la_nes are both pipe.la_nes as statistics") &&
             refused(export_model<Toy<CLASHING_ENCODINGS>>(),
                     "encodings 'array push' and 'array_push' are both committed.array_push as statistics"),
         "create refuses two units, two ports or two encodings whose names differ only where stat_name() prints '_'");
}

void within_capacity() {
  Driver d(1, false);
  bool ok = true, moved = false;
  for (int c = 0; c < 3000; c++) {
    d.cycle();
    const std::vector<uint64_t> v = read(d.m, d.self);
    for (size_t p = 0; p < 3; p++) ok = ok && v[4 * p] <= v[4 * p + 1] * v[4 * p + 2] && v[4 * p + 2] == d.now;
    moved = moved || (v[0] && v[4] && v[8] && v[11]);
  }
  expect(ok && moved, "admitted <= capacity * cycles at every cycle, and cycles is the ticks so far");
}

// A reset gives the ports their room again within the cycle; each room used again counts, so utilization stays <= 1.
void reset_reopens_a_room() {
  const vcix_model *m = export_model<Toy<GOOD>>();
  char error[128] = "";
  void *self = m->create(&NO_CONFIG, error, sizeof error);
  const Insn op = {bits_of(OP), 4, 32, 0};
  int accepted = 0;
  m->tick(self, 1);
  for (int round = 0; round < 5; round++) {
    for (int k = 0; k < 3; k++)
      if (m->can_accept(self, &op, 1)) {
        m->issue(self, &op, ++accepted, 1);
      }
    m->reset(self);
  }
  m->reset(self);
  m->tick(self, 2);
  m->reset(self);
  if (m->can_accept(self, &op, 2)) m->issue(self, &op, ++accepted, 2);
  const std::vector<uint64_t> v = read(m, self);
  m->destroy(self);
  expect(accepted == 11 && v[0] == 11 && v[1] == 2 && v[2] == 6 && v[4] == 44 && v[5] == 8 && v[6] == 6 && v[10] == 2,
         "issue, reset and issue again in one cycle: 10 admitted at 2 a cycle reads 5 cycles there, a reset with "
         "nothing admitted adds none, and admitted <= capacity * cycles");
  if (!(v[0] == 11 && v[2] == 6)) printf("accepted %d admitted %llu cycles %llu\n", accepted, (unsigned long long)v[0],
                                         (unsigned long long)v[2]);
}

void squash_restores() {
  bool same = true;
  for (unsigned seed = 1; seed <= 40; seed++) {
    Driver spec(seed, false), plain(seed, false);
    std::mt19937 wrong(seed * 7919);
    for (int c = 0; c < 400; c++) {
      const int phase = c % 23;
      const bool held = phase >= 5 && phase <= 12;
      spec.cycle(phase >= 5 && phase < 9 ? &wrong : nullptr, held);
      plain.cycle(nullptr, held);
      if (phase == 12) spec.squash_wrong();
      if (phase < 5 || phase > 12) same = same && read(spec.m, spec.self) == read(plain.m, plain.self);
    }
    same = same && spec.trace == plain.trace;
  }
  expect(same, "a wrong path issued over four cycles and squashed four later leaves every counter as if never issued");
}

void registration_follows_copies() {
  Instance<Toy<GOOD>> instance;
  instance.configure(Config(&NO_CONFIG));
  const auto inside = [](const Toy<GOOD> &model) {
    for (const Port *p : model.ports())
      if (reinterpret_cast<const char *>(p) < reinterpret_cast<const char *>(&model) ||
          reinterpret_cast<const char *>(p) >= reinterpret_cast<const char *>(&model + 1))
        return false;
    return true;
  };
  Toy<GOOD> copy = instance.model;
  const Insn op = {bits_of(OP), 5, 32, 0};
  copy.issue(op, 1, 3);
  instance.model = copy;
  std::vector<uint64_t> values(instance.stats().size());
  instance.read_stats(values.data());
  expect(inside(instance.model) && inside(copy) && values[0] == 1 && values[4] == 5,
         "after a copy-assignment the ports are the live model's, and the counts are the copy's");
}

void reads_change_nothing() {
  Driver quiet(5, false), asked(5, true);
  std::mt19937 wrong_q(11), wrong_a(11);
  for (int c = 0; c < 2000; c++) {
    const bool on = c % 31 < 3, held = c % 31 <= 6;
    quiet.cycle(on ? &wrong_q : nullptr, held);
    asked.cycle(on ? &wrong_a : nullptr, held);
    if (c % 31 == 6) {
      quiet.squash_wrong();
      asked.squash_wrong();
    }
  }
  expect(quiet.trace == asked.trace && read(quiet.m, quiet.self) == read(asked.m, asked.self),
         "reading the statistics, three times before each call, changes no answer and no count");
}

void the_dump() {
  Instance<Toy<GOOD>> instance;
  instance.configure(Config(&NO_CONFIG));
  const Insn op = {bits_of(OP), 4, 32, 0}, push = {bits_of(PUSH), 2, 32, 0};
  instance.tick(1);
  instance.issue(op, 1, 1);
  instance.issue(push, 2, 1);
  for (Cycle c = 2; c <= 4; c++) instance.tick(c);
  instance.commit(op, 1, 4);
  instance.commit(push, 2, 4);
  char text[4096] = "";
  FILE *out = fmemopen(text, sizeof text - 1, "w");
  instance.dump_stats(out);
  fclose(out);
  const char *want =
      "vcix.pipe.utilization                            0.125000  # issue: admitted / (capacity * cycles)\n"
      "vcix.pipe.issue.admitted                         1  # instructions\n"
      "vcix.pipe.issue.capacity                         2  # instructions per cycle\n"
      "vcix.pipe.issue.cycles                           4\n"
      "vcix.pipe.issue.occupancy                        0  # instructions held, summed over cycles\n"
      "vcix.pipe.issue.primary                          1\n"
      "vcix.pipe.issue.utilization                      0.125000\n"
      "vcix.pipe.lanes.admitted                         4  # elements\n"
      "vcix.pipe.lanes.capacity                         8  # elements per cycle\n"
      "vcix.pipe.lanes.cycles                           4\n"
      "vcix.pipe.lanes.occupancy                        0  # elements held, summed over cycles\n"
      "vcix.pipe.lanes.primary                          0\n"
      "vcix.pipe.lanes.utilization                      0.125000\n"
      "vcix.array.utilization                           0.500000  # entry: admitted / (capacity * cycles)\n"
      "vcix.array.entry.admitted                        2  # rows\n"
      "vcix.array.entry.capacity                        1  # rows per cycle\n"
      "vcix.array.entry.cycles                          4\n"
      "vcix.array.entry.occupancy                       5  # rows held, summed over cycles\n"
      "vcix.array.entry.primary                         1\n"
      "vcix.array.entry.utilization                     0.500000\n"
      "vcix.committed.op                                1\n"
      "vcix.committed.array_push                        1\n"
      "vcix.committed.pop                               0\n";
  expect(std::string(text) == want, "dump_stats prints the gem5 names, the primary port's utilization per unit first");
  if (std::string(text) != want) printf("%s", text);
}

}  // namespace

int main() {
  aborts_beyond_room();
  the_list();
  within_capacity();
  reset_reopens_a_room();
  squash_restores();
  registration_follows_copies();
  reads_change_nothing();
  the_dump();
  return failed;
}
