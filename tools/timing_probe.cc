// Drives the timing face without gem5: N copies of one instruction, one at a
// time (nothing is ever in flight). Each is issued when the model accepts it and
// committed latency cycles later.
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

#include "vcix_accel.h"

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s model.so insn-hex [count [lmul-log2]]\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  auto entry = reinterpret_cast<const vcix_model *(*)()>(dlsym(lib, "vcix_accel_model"));
  if (!entry) {
    fprintf(stderr, "%s does not export vcix_accel_model\n", argv[1]);
    return 1;
  }
  const vcix_model *m = entry();
  vcix_config no_config = {nullptr, [](void *, const char *) -> const char * { return nullptr; }};
  m->configure(m->self, &no_config);
  int count = argc > 3 ? atoi(argv[3]) : 4;
  vcix_insn decoded = {static_cast<uint32_t>(strtoul(argv[2], nullptr, 16)), 8, 32, argc > 4 ? atoi(argv[4]) : 0};
  const vcix_insn *insn = &decoded;

  vcix_cycle_t now = 0;
  for (int i = 0; i < count; i++) {
    while (!m->can_accept(m->self, insn, now, nullptr, 0)) now++;
    vcix_cycle_t lat = m->latency(m->self, insn, now, nullptr, 0);
    printf("insn %d: issued at %llu, committed at %llu\n", i, (unsigned long long)now, (unsigned long long)(now + lat));
    now += lat;
    m->commit(m->self, insn, now);
  }
  return 0;
}
