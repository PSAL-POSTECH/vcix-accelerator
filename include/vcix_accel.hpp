// C++ face of vcix_accel.h: subclass Model, then VCIX_ACCEL_REGISTER(YourModel).
#ifndef VCIX_ACCEL_HPP
#define VCIX_ACCEL_HPP

#include <cstdlib>
#include <string>
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

class Config {
 public:
  explicit Config(const vcix_config *c) : c_(c) {}
  // The value as written in the machine description, or nullptr.
  const char *get(const std::string &key) const { return c_->get(c_->ctx, key.c_str()); }
  uint64_t uint(const std::string &key, uint64_t fallback) const {
    const char *value = get(key);
    return value ? std::strtoull(value, nullptr, 0) : fallback;
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

template <class M>
const vcix_model *export_model() {
  static M model;
  static const std::vector<Encoding> encodings = model.owns();
  static const vcix_model table = {
      VCIX_ACCEL_ABI_VERSION,
      model.name(),
      &model,
      encodings.data(),
      encodings.size(),
      [](void *s, const vcix_config *c) { static_cast<M *>(s)->configure(Config(c)); },
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

#define VCIX_ACCEL_REGISTER(ModelClass) \
  extern "C" const vcix_model *vcix_accel_model(void) { return vcix_accel::export_model<ModelClass>(); }

#endif
