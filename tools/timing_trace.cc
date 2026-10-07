// Drives a model's timing face from a seed, as an in-order core would, and prints every answer cycle by cycle.
// One refused 64 cycles in a row is dropped. Two builds that print the same trace answered the script alike.
// Usage: timing_trace model.so seed cycles [key=value ...]
#include <dlfcn.h>

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "vcix_accel.h"

namespace {

struct InFlight {
  vcix_id_t id;
  vcix_insn insn;
  vcix_cycle_t ready_at;
  bool known;
};

struct Tally {
  uint64_t accepted = 0, refused = 0;
};

bool parse(const char *text, uint64_t &number) {
  const char *end = text + strlen(text);
  const std::from_chars_result parsed = std::from_chars(text, end, number, 10);
  return parsed.ec == std::errc() && parsed.ptr == end;
}

// Pushes, pops or the rest, by the encoding's name: the phases of the script lean on one of the three.
int kind_of(const char *name) {
  if (strstr(name, "push")) return 0;
  if (strstr(name, "pop")) return 1;
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  uint64_t seed = 0, cycles = 0;
  if (argc < 4 || !parse(argv[2], seed) || !parse(argv[3], cycles)) {
    fprintf(stderr, "usage: %s model.so seed cycles [key=value ...]\n", argv[0]);
    return 2;
  }
  std::map<std::string, std::string> config;
  for (int i = 4; i < argc; i++) {
    const char *eq = strchr(argv[i], '=');
    if (!eq) {
      fprintf(stderr, "%s: '%s' is not key=value\n", argv[0], argv[i]);
      return 2;
    }
    config[std::string(argv[i], static_cast<size_t>(eq - argv[i]))] = eq + 1;
  }

  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  auto entry = reinterpret_cast<const vcix_model *(*)()>(dlsym(lib, "vcix_accel_model"));
  const vcix_model *m = entry ? entry() : nullptr;
  if (!m || m->abi_version != VCIX_ACCEL_ABI_VERSION) {
    fprintf(stderr, "%s: no table of ABI %u\n", argv[1], VCIX_ACCEL_ABI_VERSION);
    return 1;
  }
  vcix_config described = {&config, [](void *ctx, const char *key) -> const char * {
                             const auto &all = *static_cast<std::map<std::string, std::string> *>(ctx);
                             const auto found = all.find(key);
                             return found == all.end() ? nullptr : found->second.c_str();
                           }};
  char error[256] = "";
  void *self = m->create(&described, error, sizeof error);
  if (!self) {
    fprintf(stderr, "%s: %s\n", argv[1], error);
    return 1;
  }

  std::mt19937_64 random(seed);
  const auto pick = [&](uint64_t n) { return random() % n; };
  std::vector<std::vector<size_t>> by_kind(3);
  for (size_t e = 0; e < m->num_encodings; e++) by_kind[kind_of(m->encodings[e].name)].push_back(e);
  std::vector<Tally> tally(m->num_encodings);
  static int even[3] = {1, 1, 1};
  const auto next_insn = [&](int *weights, size_t &which) {
    const auto sum = [&](const int *w) { return (by_kind[0].empty() ? 0 : w[0]) + (by_kind[1].empty() ? 0 : w[1]) + (by_kind[2].empty() ? 0 : w[2]); };
    if (sum(weights) == 0) weights = even;
    const int total = sum(weights);
    int kind = 0;
    for (int roll = static_cast<int>(pick(total));; kind++)
      if (!by_kind[kind].empty() && (roll -= weights[kind]) < 0) break;
    which = by_kind[kind][pick(by_kind[kind].size())];
    const vcix_encoding &e = m->encodings[which];
    return vcix_insn{e.match | (static_cast<uint32_t>(random()) & ~e.mask), static_cast<uint32_t>(1 + pick(12)),
                     8u << pick(3), static_cast<int32_t>(pick(4)) - 1};
  };

  std::deque<InFlight> in_flight;
  vcix_id_t next_id = 1;
  int weights[3] = {1, 1, 1};
  size_t pending_which = 0;
  uint64_t waited = 0;
  vcix_insn pending = next_insn(weights, pending_which);
  for (vcix_cycle_t now = 1; now <= cycles; now++) {
    if (now % 256 == 1) {
      static const int phases[][3] = {{8, 1, 2}, {1, 8, 1}, {2, 2, 2}, {8, 0, 1}, {0, 1, 8}};
      const int *phase = phases[pick(5)];
      for (int k = 0; k < 3; k++) weights[k] = phase[k];
      printf("%" PRIu64 " phase %d %d %d\n", now, weights[0], weights[1], weights[2]);
    }
    if (m->tick) m->tick(self, now);
    printf("%" PRIu64 ":", now);
    for (InFlight &f : in_flight)
      if (!f.known && m->ready && m->ready(self, f.id, now)) {
        f.known = true;
        f.ready_at = now;
        printf(" r%" PRIu64, f.id);
      }
    for (int k = 0; k < 2 && !in_flight.empty() && in_flight.front().known && in_flight.front().ready_at <= now; k++) {
      m->commit(self, &in_flight.front().insn, in_flight.front().id, now);
      printf(" c%" PRIu64, in_flight.front().id);
      in_flight.pop_front();
    }
    if (!in_flight.empty() && pick(40) == 0) {
      const vcix_id_t first = in_flight[pick(in_flight.size())].id;
      m->squash(self, first, now);
      while (!in_flight.empty() && in_flight.back().id >= first) in_flight.pop_back();
      printf(" s%" PRIu64, first);
    }
    if (m->reset && pick(4000) == 0) {
      m->reset(self);
      in_flight.clear();
      printf(" reset");
    }
    for (uint64_t width = 1 + pick(4); width; width--) {
      const bool accepted = m->can_accept(self, &pending, now);
      printf(" %08" PRIx32 "/%u:%d", pending.bits, pending.vl, accepted);
      if (!accepted) {
        tally[pending_which].refused++;
        if (++waited == 64) {
          printf(" drop");
          pending = next_insn(weights, pending_which);
          waited = 0;
        }
        break;
      }
      waited = 0;
      tally[pending_which].accepted++;
      const vcix_cycle_t latency = m->issue(self, &pending, next_id, now);
      const bool known = latency != VCIX_LATENCY_UNKNOWN;
      printf("=%" PRIu64, latency);
      in_flight.push_back({next_id++, pending, known ? now + latency : 0, known});
      pending = next_insn(weights, pending_which);
    }
    printf("\n");
  }
  for (size_t e = 0; e < m->num_encodings; e++)
    printf("%s: accepted %" PRIu64 ", refused %" PRIu64 "\n", m->encodings[e].name, tally[e].accepted, tally[e].refused);
  m->destroy(self);
  return 0;
}
