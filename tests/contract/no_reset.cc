// Test model: a table filled in by hand, as a model not written against the C++
// wrapper would be, with the one member that may be NULL left NULL. It keeps no state.
#include "vcix_accel.h"

namespace {

const vcix_encoding encodings[] = {{0x0600405B, 0xFE00707F, "owned"}};

int stateless;

const vcix_model table = {
    VCIX_ACCEL_ABI_VERSION,
    "no_reset",
    encodings,
    1,
    [](const vcix_config *, char *, size_t) -> void * { return &stateless; },
    [](void *) {},
    [](void *, const vcix_host *, const vcix_insn *) {},
    [](void *, const vcix_insn *, vcix_cycle_t, const vcix_pending *, size_t) -> int { return 1; },
    [](void *, const vcix_insn *, vcix_cycle_t, const vcix_pending *, size_t) -> vcix_cycle_t { return 2; },
    [](void *, const vcix_insn *, vcix_cycle_t) {},
    nullptr,  // reset
};

}  // namespace

extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) { return &table; }
