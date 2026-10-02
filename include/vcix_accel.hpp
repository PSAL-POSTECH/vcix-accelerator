// C++ face of vcix_accel.h: subclass Model, then VCIX_ACCEL_REGISTER(YourModel).
#ifndef VCIX_ACCEL_HPP
#define VCIX_ACCEL_HPP

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "vcix_accel.h"

namespace vcix_accel {

using Cycle = vcix_cycle_t;
using Encoding = vcix_encoding;
using Insn = vcix_insn;

// The instructions issued to this model and not yet committed, oldest first.
class Pending {
 public:
  Pending(const vcix_pending *first, size_t count) : first_(first), count_(count) {}
  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const vcix_pending &operator[](size_t i) const { return first_[i]; }
  const vcix_pending *begin() const { return first_; }
  const vcix_pending *end() const { return first_ + count_; }

 private:
  const vcix_pending *first_;
  size_t count_;
};

inline uint32_t rd(const Insn &insn) { return (insn.bits >> 7) & 0x1f; }
inline uint32_t rs1(const Insn &insn) { return (insn.bits >> 15) & 0x1f; }
inline uint32_t rs2(const Insn &insn) { return (insn.bits >> 20) & 0x1f; }
inline uint32_t funct3(const Insn &insn) { return (insn.bits >> 12) & 0x7; }
// Registers in one vector operand's group.
inline uint32_t group_size(const Insn &insn) { return insn.lmul_log2 > 0 ? 1u << insn.lmul_log2 : 1u; }

// A value of the machine description the model cannot use. Thrown from
// configure, it stops the run.
class ConfigError : public std::runtime_error {
 public:
  ConfigError(const std::string &key, const std::string &value, const std::string &why)
      : std::runtime_error("machine description: " + key + ": '" + value + "' " + why) {}
};

class Config {
 public:
  explicit Config(const vcix_config *c) : c_(c) {}
  // The value as written, or nullptr when the key is absent. Valid until configure returns.
  const char *get(const std::string &key) const { return c_->get(c_->ctx, key.c_str()); }
  // The value as an unsigned decimal number; `fallback` only for an absent key.
  // Text that is not such a number throws ConfigError.
  uint64_t uint(const std::string &key, uint64_t fallback) const {
    const char *value = get(key);
    if (!value) return fallback;
    const char *end = value + std::strlen(value);
    uint64_t number = 0;
    const std::from_chars_result parsed = std::from_chars(value, end, number, 10);
    if (parsed.ec == std::errc::invalid_argument || parsed.ptr != end)
      throw ConfigError(key, value, "is not an unsigned decimal number");
    if (parsed.ec == std::errc::result_out_of_range) throw ConfigError(key, value, "does not fit in 64 bits");
    if (value[0] == '0' && end - value > 1)
      throw ConfigError(key, value, "has a leading zero, which reads as octal or as decimal depending on the reader");
    return number;
  }
  // The value as a hexadecimal number written 0x...; `fallback` only for an absent key.
  // Text that is not such a number throws ConfigError.
  uint64_t hex(const std::string &key, uint64_t fallback) const {
    const char *value = get(key);
    if (!value) return fallback;
    if (std::strncmp(value, "0x", 2) != 0) throw ConfigError(key, value, "is not a hexadecimal number written 0x...");
    const char *end = value + std::strlen(value);
    uint64_t number = 0;
    const std::from_chars_result parsed = std::from_chars(value + 2, end, number, 16);
    if (parsed.ec == std::errc::invalid_argument || parsed.ptr != end)
      throw ConfigError(key, value, "is not a hexadecimal number written 0x...");
    if (parsed.ec == std::errc::result_out_of_range) throw ConfigError(key, value, "does not fit in 64 bits");
    return number;
  }

 private:
  const vcix_config *c_;
};

class Host {
 public:
  explicit Host(const vcix_host *h) : h_(h) {}
  uint32_t lanes() const { return h_->lanes(h_->ctx); }
  uint32_t vlen_bits() const { return h_->vlen_bits(h_->ctx); }
  template <class T>
  const T *vreg(uint32_t lane, uint32_t reg) const {
    return static_cast<const T *>(h_->vreg(h_->ctx, lane, reg, 0));
  }
  template <class T>
  T *vreg_mut(uint32_t lane, uint32_t reg) const {
    return static_cast<T *>(h_->vreg(h_->ctx, lane, reg, 1));
  }
  uint64_t xreg(uint32_t reg) const { return h_->xreg_read(h_->ctx, reg); }
  void set_xreg(uint32_t reg, uint64_t v) const { h_->xreg_write(h_->ctx, reg, v); }
  uint64_t freg_bits(uint32_t reg) const { return h_->freg_bits(h_->ctx, reg); }
  void mem_read(uint64_t addr, void *dst, size_t n) const { h_->mem_read(h_->ctx, addr, dst, n); }
  void mem_write(uint64_t addr, const void *src, size_t n) const { h_->mem_write(h_->ctx, addr, src, n); }

 private:
  const vcix_host *h_;
};

// name() and owns() are asked once, before configure, so they cannot depend on
// the machine description. The strings they return must outlive the library.
class Model {
 public:
  virtual ~Model() = default;
  virtual const char *name() const = 0;
  virtual std::vector<Encoding> owns() const = 0;
  virtual void configure(const Config &) {}

  virtual void execute(const Host &host, const Insn &insn) = 0;

  virtual bool can_accept(const Insn &insn, Cycle now, const Pending &pending) const = 0;
  virtual Cycle latency(const Insn &insn, Cycle now, const Pending &pending) const = 0;
  virtual void commit(const Insn &insn, Cycle now) = 0;

  virtual void reset() {}
};

// The table of model M, this library's alone. A ConfigError from configure ends
// the process with a message: the C ABI's configure cannot report it.
template <class M>
__attribute__((visibility("hidden"))) const vcix_model *export_model() {
  static M model;
  static const std::vector<Encoding> encodings = model.owns();
  static const vcix_model table = {
      VCIX_ACCEL_ABI_VERSION,
      model.name(),
      &model,
      encodings.data(),
      encodings.size(),
      [](void *s, const vcix_config *c) {
        M *m = static_cast<M *>(s);
        try {
          m->configure(Config(c));
        } catch (const ConfigError &e) {
          std::fprintf(stderr, "%s: %s\n", m->name(), e.what());
          std::exit(1);
        }
      },
      [](void *s, const vcix_host *h, const vcix_insn *i) { static_cast<M *>(s)->execute(Host(h), *i); },
      [](void *s, const vcix_insn *i, Cycle n, const vcix_pending *p, size_t c) -> int {
        return static_cast<M *>(s)->can_accept(*i, n, Pending(p, c));
      },
      [](void *s, const vcix_insn *i, Cycle n, const vcix_pending *p, size_t c) -> Cycle {
        return static_cast<M *>(s)->latency(*i, n, Pending(p, c));
      },
      [](void *s, const vcix_insn *i, Cycle n) { static_cast<M *>(s)->commit(*i, n); },
      [](void *s) { static_cast<M *>(s)->reset(); },
  };
  return &table;
}

}  // namespace vcix_accel

// Defines the one symbol a model library exports, with default visibility.
#define VCIX_ACCEL_REGISTER(ModelClass)                                                    \
  extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) { \
    return vcix_accel::export_model<ModelClass>();                                         \
  }

#endif
