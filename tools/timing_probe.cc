// Drives the timing face without gem5: N copies of one instruction, one at a
// time. Each is issued when the model accepts it and committed latency cycles later.
#include <dlfcn.h>

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <system_error>

#include "vcix_accel.h"

namespace {

// The whole of `text` as a number in `base`, within [lowest, highest].
template <class T>
bool parse(const char *text, int base, T lowest, T highest, T &number) {
  const char *end = text + strlen(text);
  const std::from_chars_result parsed = std::from_chars(text, end, number, base);
  return parsed.ec == std::errc() && parsed.ptr == end && number >= lowest && number <= highest;
}

int usage(const char *self, const char *what, const char *text) {
  if (what) fprintf(stderr, "%s: '%s' is not %s\n", self, text, what);
  fprintf(stderr, "usage: %s model.so insn-hex [count [lmul-log2]]\n", self);
  return 2;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3 || argc > 5) return usage(argv[0], nullptr, nullptr);

  const char *hex = argv[2];
  if (strncmp(hex, "0x", 2) == 0) hex += 2;
  uint32_t bits = 0;
  if (!parse<uint32_t>(hex, 16, 0, UINT32_MAX, bits)) return usage(argv[0], "an instruction: 32 bits in hex", argv[2]);
  uint32_t count = 4;
  if (argc > 3 && !parse<uint32_t>(argv[3], 10, 0, UINT32_MAX, count)) return usage(argv[0], "a count", argv[3]);
  int32_t lmul_log2 = 0;
  if (argc > 4 && !parse<int32_t>(argv[4], 10, -3, 3, lmul_log2)) return usage(argv[0], "an lmul-log2, -3 to 3", argv[4]);

  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
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
  if (m->abi_version != VCIX_ACCEL_ABI_VERSION) {
    fprintf(stderr, "%s has ABI %u, the probe has %u\n", argv[1], m->abi_version, VCIX_ACCEL_ABI_VERSION);
    return 1;
  }
  if (!m->can_accept || !m->latency || !m->commit) {
    fprintf(stderr, "%s leaves part of the timing face NULL\n", argv[1]);
    return 1;
  }
  if (!vcix_owner(m, bits)) {
    fprintf(stderr, "%s does not own instruction %08" PRIx32 "\n", argv[1], bits);
    return 1;
  }

  vcix_config no_config = {nullptr, [](void *, const char *) -> const char * { return nullptr; }};
  if (m->configure) m->configure(m->self, &no_config);
  if (m->reset) m->reset(m->self);

  const vcix_insn decoded = {bits, 8, 32, lmul_log2};
  const vcix_insn *insn = &decoded;

  vcix_cycle_t now = 0;
  for (uint32_t i = 0; i < count; i++) {
    while (!m->can_accept(m->self, insn, now, nullptr, 0)) now++;
    vcix_cycle_t lat = m->latency(m->self, insn, now, nullptr, 0);
    printf("insn %" PRIu32 ": issued at %" PRIu64 ", committed at %" PRIu64 "\n", i, now, now + lat);
    now += lat;
    m->commit(m->self, insn, now);
  }
  return 0;
}
