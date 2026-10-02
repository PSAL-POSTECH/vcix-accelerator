// The systolic array: weights of fewer columns than lanes, a full matrix, a matrix that slides, pops in
// pieces, and every element width. Output: what each pop left in every lane.
#include "common.h"

kernel k_input_8, k_input_16, k_input_32, k_input_64, k_weight_8, k_weight_16, k_weight_32, k_weight_64;
kernel k_pop_8, k_pop_16, k_pop_32, k_pop_64;
void k_compute(void);

#define IN 0ul
#define OUT 4096ul

// Element i of every lane, as a small number with a fraction: of `bytes` 1 an integer, 2 a half, 4 a single.
static uint16_t half_of(int sixteenths) {  // exact for |sixteenths| < 2048
  float value = sixteenths / 16.0f;
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  if ((bits & 0x7fffffff) == 0) return (uint16_t)(bits >> 16);
  const int exponent = (int)((bits >> 23) & 0xff) - 127 + 15;
  return (uint16_t)((bits >> 16 & 0x8000) | exponent << 10 | (bits >> 13 & 0x3ff));
}
static void fill(unsigned bytes, unsigned long count, int scale) {
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < count; i++) {
      const int sixteenths = (int)random_below(2 * scale + 1) - scale;
      if (bytes == 1) *(int8_t *)spad(lane, IN + i) = (int8_t)(sixteenths / 16);
      if (bytes == 2) *(uint16_t *)spad(lane, IN + 2 * i) = half_of(sixteenths);
      if (bytes == 4) *(float *)spad(lane, IN + 4 * i) = sixteenths / 16.0f;
      if (bytes == 8) *(double *)spad(lane, IN + 8 * i) = sixteenths / 16.0;
    }
}
static void push(kernel *function, unsigned bytes, unsigned long count, int scale) {
  fill(bytes, count, scale);
  if (function(spad(0, IN), 0, count) != count) puts("a push did not take every element");
}
static void pop(kernel *function, unsigned bytes, unsigned long count) {
  for (unsigned lane = 0; lane < LANES; lane++) memset(spad(lane, OUT), 0xA5, 64);
  if (function(0, spad(0, OUT), count) != count) puts("a pop did not take every element");
  emit_lanes(OUT, 64);
}

int main(void) {
  // Two columns of weights: a lane's output is two products.
  push(k_weight_32, 4, 2, 64);
  push(k_input_32, 4, 3, 64);
  k_compute();
  pop(k_pop_32, 4, 3);
  // Two more: the matrix is full. Then three more: the oldest three columns are gone.
  push(k_weight_32, 4, 2, 64);
  push(k_input_32, 4, 5, 64);
  pop(k_pop_32, 4, 2);
  pop(k_pop_32, 4, 3);
  push(k_weight_32, 4, 3, 64);
  push(k_input_32, 4, 4, 64);
  push(k_input_32, 4, 1, 64);
  pop(k_pop_32, 4, 5);
  // More elements than a register holds.
  push(k_weight_32, 4, 4, 64);
  push(k_input_32, 4, 16, 64);
  pop(k_pop_32, 4, 16);
  // Halves; the last inputs are large enough for a sum past the largest half.
  push(k_weight_16, 2, 4, 2047);
  push(k_input_16, 2, 6, 64);
  pop(k_pop_16, 2, 6);
  push(k_input_16, 2, 4, 2047);
  pop(k_pop_16, 2, 4);
  // Bytes, small enough that every sum is one.
  push(k_weight_8, 1, 4, 48);
  push(k_input_8, 1, 8, 48);
  pop(k_pop_8, 1, 8);
  // Pushed as one width, popped as another; a pop of doublewords writes singles.
  push(k_weight_16, 2, 4, 64);
  push(k_input_32, 4, 6, 64);
  pop(k_pop_16, 2, 2);
  pop(k_pop_64, 8, 2);
  pop(k_pop_8, 1, 2);
  // A push of doublewords is a push of zeros.
  push(k_weight_64, 8, 1, 64);
  push(k_input_64, 8, 2, 64);
  pop(k_pop_32, 4, 2);
  return 0;
}
