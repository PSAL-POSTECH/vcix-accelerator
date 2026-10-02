// Loads two model libraries the way the adapters do and checks that each one
// answers with a table of its own. Usage: load_two first.so second.so
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
  if (!ok) failed = 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s first.so second.so\n", argv[0]);
    return 2;
  }
  const vcix_model *first = load(argv[1]);
  const vcix_model *second = load(argv[2]);
  if (!first || !second) return 2;

  const vcix_insn insn = {0, 8, 32, 0};
  const vcix_config no_config = {nullptr, [](void *, const char *) -> const char * { return nullptr; }};
  char error[64] = "";
  void *one = first->create(&no_config, error, sizeof error);
  void *two = second->create(&no_config, error, sizeof error);
  expect(first != second, "each library has its own table");
  expect(one && two && one != two, "each library makes its own instance");
  if (!one || !two) return 1;
  expect(strcmp(first->name, "first") == 0 && strcmp(second->name, "second") == 0, "each table carries its own name");
  expect(first->encodings != second->encodings && first->encodings[0].match != second->encodings[0].match,
         "each table carries its own encodings");
  expect(first->latency(one, &insn, 0, nullptr, 0) == 1 && second->latency(two, &insn, 0, nullptr, 0) == 2,
         "each table calls its own model");
  return failed;
}
