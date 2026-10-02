#ifndef TPU_STREAM_HPP
#define TPU_STREAM_HPP

#include <cstdint>
#include <vector>

namespace tpu {

class Stream {
 public:
  Stream(uint32_t slots, uint32_t capacity) { configure(slots, capacity); }
  void configure(uint32_t slots, uint32_t capacity) {
    slots_ = slots;
    capacity_ = capacity;
    line_.assign((uint64_t{slots} + 63) / 64, 0);
    head_ = 0;
  }

  bool has_room(uint32_t elements) const { return elements <= capacity_ - input_; }
  bool holds(uint32_t elements) const { return elements <= output_; }
  void push(uint32_t elements) { input_ += elements; }
  void pop(uint32_t elements) { output_ -= elements; }
  void tick() {
    if (output_ == capacity_) return;
    uint64_t &word = line_[head_ / 64];
    const uint64_t slot = uint64_t{1} << (head_ % 64);
    if (word & slot) output_++;
    if (input_) {
      word |= slot;
      input_--;
    } else {
      word &= ~slot;
    }
    head_ = head_ + 1 == slots_ ? 0 : head_ + 1;
  }
  void reset() {
    input_ = output_ = 0;
    line_.assign(line_.size(), 0);
    head_ = 0;
  }

  uint32_t input_entries() const { return input_; }
  uint32_t output_entries() const { return output_; }
  uint32_t slots() const { return slots_; }
  uint32_t queue_capacity() const { return capacity_; }

 private:
  uint32_t slots_ = 0;
  uint32_t capacity_ = 0;
  uint32_t input_ = 0;
  uint32_t output_ = 0;
  uint32_t head_ = 0;
  std::vector<uint64_t> line_;
};

}  // namespace tpu

#endif
