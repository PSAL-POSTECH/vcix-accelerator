// Spike adapter: the extension `vcixaccel` forwards every encoding the loaded
// model owns to its functional face. The model is the .so named by VCIX_ACCEL_MODEL;
// VCIX_ACCEL_CONFIG names the machine description (YAML) it is configured from.
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include <yaml-cpp/yaml.h>

#include "extension.h"
#include "mmu.h"
#include "processor.h"
#include "trap.h"
#include "vcix_accel.h"

namespace {

const vcix_model *g_model = nullptr;

processor_t *proc(void *ctx) { return static_cast<processor_t *>(ctx); }

const vcix_host host_template = {
    nullptr,
    [](void *c) -> uint32_t { return proc(c)->VU.get_vu_num(); },
    [](void *c) -> uint32_t { return proc(c)->VU.get_vlen(); },
    [](void *c, uint32_t lane, uint32_t reg, int w) -> void * { return &proc(c)->VU.elt<uint8_t>(reg, 0, lane, w); },
    [](void *c, uint32_t reg) -> uint64_t { return proc(c)->get_state()->XPR[reg]; },
    [](void *c, uint32_t reg, uint64_t v) { proc(c)->get_state()->XPR.write(reg, v); },
    [](void *c, uint32_t reg) -> uint64_t { return proc(c)->get_state()->FPR[reg].v[0]; },
    [](void *c, uint64_t addr, void *dst, size_t n) {
      for (size_t i = 0; i < n; i++) static_cast<uint8_t *>(dst)[i] = proc(c)->get_mmu()->load_uint8(addr + i);
    },
    [](void *c, uint64_t addr, const void *src, size_t n) {
      for (size_t i = 0; i < n; i++) proc(c)->get_mmu()->store_uint8(addr + i, static_cast<const uint8_t *>(src)[i]);
    },
};

reg_t dispatch(processor_t *p, insn_t insn, reg_t pc) {
  uint32_t bits = static_cast<uint32_t>(insn.bits());
  if (!vcix_owner(g_model, bits)) throw trap_illegal_instruction(bits);
  vcix_host host = host_template;
  host.ctx = p;
  // vlmul is the low three bits of vtype, signed.
  int vlmul = static_cast<int>(p->VU.vtype->read() & 0x7);
  vcix_insn decoded = {bits, static_cast<uint32_t>(p->VU.vl->read()), static_cast<uint32_t>(p->VU.vsew),
                       vlmul >= 4 ? vlmul - 8 : vlmul};
  g_model->execute(g_model->self, &host, &decoded);
  return pc + 4;
}

// Top-level scalars of the machine description, each as written in the file.
std::map<std::string, std::string> load_config() {
  std::map<std::string, std::string> values;
  const char *path = getenv("VCIX_ACCEL_CONFIG");
  if (!path) return values;
  YAML::Node root = YAML::LoadFile(path);
  for (const auto &entry : root)
    if (entry.second.IsScalar()) values[entry.first.Scalar()] = entry.second.Scalar();
  return values;
}

const vcix_model *load_model() {
  const char *path = getenv("VCIX_ACCEL_MODEL");
  if (!path) {
    fprintf(stderr, "vcixaccel: VCIX_ACCEL_MODEL is not set\n");
    exit(1);
  }
  void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "vcixaccel: %s\n", dlerror());
    exit(1);
  }
  auto entry = reinterpret_cast<const vcix_model *(*)()>(dlsym(lib, "vcix_accel_model"));
  if (!entry) {
    fprintf(stderr, "vcixaccel: %s does not export vcix_accel_model\n", path);
    exit(1);
  }
  const vcix_model *m = entry();
  if (m->abi_version != VCIX_ACCEL_ABI_VERSION) {
    fprintf(stderr, "vcixaccel: %s has ABI %u, adapter has %u\n", path, m->abi_version, VCIX_ACCEL_ABI_VERSION);
    exit(1);
  }
  std::map<std::string, std::string> values = load_config();
  vcix_config config = {&values, [](void *ctx, const char *key) -> const char * {
                          auto &map = *static_cast<std::map<std::string, std::string> *>(ctx);
                          auto found = map.find(key);
                          return found == map.end() ? nullptr : found->second.c_str();
                        }};
  m->configure(m->self, &config);
  return m;
}

class vcix_accel_extension_t : public extension_t {
 public:
  vcix_accel_extension_t() {
    if (!g_model) g_model = load_model();
  }
  const char *name() override { return "vcixaccel"; }
  void reset() override {
    if (g_model->reset) g_model->reset(g_model->self);
  }
  std::vector<insn_desc_t> get_instructions() override {
    std::vector<insn_desc_t> insns;
    for (size_t i = 0; i < g_model->num_encodings; i++) {
      const vcix_encoding &e = g_model->encodings[i];
      insns.push_back((insn_desc_t){e.match, e.mask, dispatch, dispatch});
    }
    return insns;
  }
  std::vector<disasm_insn_t *> get_disasms() override {
    std::vector<disasm_insn_t *> insns;
    for (size_t i = 0; i < g_model->num_encodings; i++) {
      const vcix_encoding &e = g_model->encodings[i];
      insns.push_back(new disasm_insn_t(e.name, e.match, e.mask, {}));
    }
    return insns;
  }
};

}  // namespace

REGISTER_EXTENSION(vcixaccel, []() { return new vcix_accel_extension_t; })
