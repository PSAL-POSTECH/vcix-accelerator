// Checks that instances of one model library share no state, and that a refusal says why.
// Usage: instances remembers.so refuses.so
#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#include "vcix_accel.h"

namespace {

const vcix_model *load(const char *path) {
  void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "%s\n", dlerror());
    return nullptr;
  }
  auto entry = reinterpret_cast<const vcix_model *(*)()>(dlsym(lib, "vcix_accel_model"));
  if (!entry) {
    fprintf(stderr, "%s does not export vcix_accel_model\n", path);
    return nullptr;
  }
  return entry();
}

int failed = 0;

void expect(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  fflush(stdout);
  if (!ok) failed = 1;
}

const vcix_config no_config = {nullptr, [](void *, const char *) -> const char * { return nullptr; }};
const vcix_insn insn = {0x062541db, 8, 32, 0};

vcix_id_t next_id = 1;

// What remembers answers to an issue, taken back at once: 10 plus the commits the instance has seen.
vcix_cycle_t commits_seen(const vcix_model *m, void *self) {
  const vcix_id_t id = next_id++;
  const vcix_cycle_t answer = m->issue(self, &insn, id, 0);
  m->squash(self, id, 0);
  return answer - 10;
}

void issue_and_commit(const vcix_model *m, void *self, vcix_cycle_t now) {
  const vcix_id_t id = next_id++;
  m->issue(self, &insn, id, now);
  m->commit(self, &insn, id, now);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s remembers.so refuses.so\n", argv[0]);
    return 2;
  }
  const vcix_model *remembers = load(argv[1]);
  const vcix_model *refuses = load(argv[2]);
  if (!remembers || !refuses) return 2;

  char error[64] = "";
  void *one = remembers->create(&no_config, error, sizeof error);
  void *two = remembers->create(&no_config, error, sizeof error);
  if (!one || !two) {
    fprintf(stderr, "%s: no instance: %s\n", argv[1], error);
    return 2;
  }
  expect(one != two, "one library makes two instances");

  issue_and_commit(remembers, one, 5);
  expect(commits_seen(remembers, one) == 1 && commits_seen(remembers, two) == 0,
         "a commit on one instance does not change what the other answers");

  issue_and_commit(remembers, two, 6);
  issue_and_commit(remembers, two, 7);
  remembers->destroy(one);
  one = remembers->create(&no_config, error, sizeof error);
  expect(one && commits_seen(remembers, one) == 0 && commits_seen(remembers, two) == 2,
         "destroy then create gives a fresh instance and leaves the other as it was");

  remembers->reset(two);
  expect(commits_seen(remembers, two) == 0, "reset returns an instance to the state create left");
  remembers->destroy(one);
  remembers->destroy(two);

  void *none = refuses->create(&no_config, error, sizeof error);
  expect(!none && strcmp(error, "this machine has no such unit") == 0, "a model that cannot be configured makes no instance and says why");

  char small[16];
  memset(small, '#', sizeof small);
  none = refuses->create(&no_config, small, 8);
  expect(!none && memcmp(small, "this ma\0########", sizeof small) == 0, "the reason is cut to error_size and NUL-terminated");
  return failed;
}
