// Test model: owns nothing and reports, for each key below, the value it is configured
// with or that the key is absent; then `count` read as a decimal number and `base` as hex.
#include <cinttypes>
#include <cstdio>

#include "vcix_accel.hpp"

namespace {

using namespace vcix_accel;

constexpr const char *KEYS[] = {"plain",        "quoted",     "empty",        "tilde",  "null_word", "quoted_empty",
                                "quoted_null",  "quoted_tilde", "leading_zero", "spaced", "nested",    "listed",
                                "not_in_the_file"};

class ShowsConfig : public Model {
 public:
  const char *name() const override { return "shows_config"; }
  std::vector<Encoding> owns() const override { return {}; }

  void configure(const Config &config) override {
    for (const char *key : KEYS) {
      const char *value = config.get(key);
      if (value) printf("[config] %s = <%s>\n", key, value);
      else printf("[config] %s is absent\n", key);
    }
    fflush(stdout);
    printf("[number] count = %" PRIu64 "\n", config.uint("count", 5));
    fflush(stdout);
    printf("[number] base = 0x%" PRIx64 "\n", config.hex("base", 0x1000));
    fflush(stdout);
  }

  void execute(const Host &, const Insn &) override {}
  bool can_accept(const Insn &, Cycle, const Pending &) const override { return true; }
  Cycle latency(const Insn &, Cycle, const Pending &) const override { return 1; }
  void commit(const Insn &, Cycle) override {}
};

}  // namespace

VCIX_ACCEL_REGISTER(ShowsConfig)
