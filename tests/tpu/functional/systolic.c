// The systolic array: weights of fewer columns than lanes, a full matrix, a matrix that slides, pops in
// pieces, every element width, the two floats of 8 bits, and every rounding mode frm can hold.
// Output: what each pop left in every lane. With the argument bf16: bfloat16 pushes, checked against a formula.
#include "common.h"

kernel_with k_input_8, k_input_16, k_input_32, k_input_64, k_weight_8, k_weight_16, k_weight_32, k_weight_64;
kernel_with k_pop_8, k_pop_16, k_pop_32, k_pop_64;
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
// What an element is, as the push or the pop carries it; 0 is the width's own, an integer of 8 bits.
enum { INTEGER = 0, E4M3 = 1, E5M2 = 2, BF16 = 3 };
static void push_as(kernel_with *function, unsigned long count, unsigned format) {
  if (function(spad(0, IN), 0, count, format, 0) != count) puts("a push did not take every element");
}
static void push(kernel_with *function, unsigned bytes, unsigned long count, int scale) {
  fill(bytes, count, scale);
  push_as(function, count, INTEGER);
}
static void pop_as(kernel_with *function, unsigned long count, unsigned format) {
  for (unsigned lane = 0; lane < LANES; lane++) memset(spad(lane, OUT), 0xA5, 64);
  if (function(0, spad(0, OUT), count, format, 0) != count) puts("a pop did not take every element");
  emit_lanes(OUT, 64);
}
static void pop(kernel_with *function, unsigned bytes, unsigned long count) { pop_as(function, count, INTEGER); }
// Bytes that are finite in both floats of 8 bits when `finite`, any bytes otherwise.
static void fill_bytes(unsigned long count, int finite) {
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < count; i++) {
      uint8_t byte = (uint8_t)random32();
      while (finite && ((byte & 0x7c) == 0x7c || (byte & 0x7f) == 0x7f)) byte = (uint8_t)random32();
      *(uint8_t *)spad(lane, IN + i) = byte;
    }
}
static void set_frm(unsigned long mode) { __asm__ volatile("csrw frm, %0" : : "r"(mode)); }

// bfloat16 weights and inputs of exponents 2^-30 to 2^30, past what a half holds, popped as singles. Every
// product is exact, so lane j's element i must be the float sum over k of input k's element i times weight k of j.
static int bf16_case(void) {
  enum { COUNT = 16 };
  float weight[LANES][LANES], input[LANES][COUNT];
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned i = 0; i < COUNT; i++) {
      const uint32_t bits = (random32() & 0x8000u) << 16 | (97 + random_below(61)) << 23 | (random32() & 0x7fu) << 16;
      float value;
      memcpy(&value, &bits, sizeof value);
      if (i < LANES) weight[lane][i] = value;
      input[lane][i] = value;
      *(uint16_t *)spad(lane, IN + 2 * i) = (uint16_t)(bits >> 16);
    }
  push_as(k_weight_16, LANES, BF16);
  push_as(k_input_16, COUNT, BF16);
  if (k_pop_32(0, spad(0, OUT), COUNT, INTEGER, 0) != COUNT) puts("a pop did not take every element");
  unsigned wrong = 0;
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned i = 0; i < COUNT; i++) {
      float want = 0;
      for (unsigned k = 0; k < LANES; k++) want += input[k][i] * weight[lane][k];
      const float got = *(float *)spad(lane, OUT + 4 * i);
      if (memcmp(&got, &want, sizeof got)) {
        if (!wrong) printf("lane %u element %u: got %g, want %g\n", lane, i, (double)got, (double)want);
        wrong++;
      }
    }
  printf("bf16: %u of %u elements as the formula says\n", LANES * COUNT - wrong, LANES * COUNT);
  return wrong != 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "bf16")) return bf16_case();
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

  // The floats of 8 bits. Finite weights and inputs of each kind, read back as each kind and as halves and singles.
  for (unsigned format = E4M3; format <= E5M2; format++) {
    fill_bytes(4, 1);
    push_as(k_weight_8, 4, format);
    fill_bytes(16, 1);
    push_as(k_input_8, 16, format);
    pop_as(k_pop_8, 4, format);
    pop_as(k_pop_8, 4, format == E4M3 ? E5M2 : E4M3);
    pop_as(k_pop_16, 4, INTEGER);
    pop_as(k_pop_32, 4, INTEGER);
    // Any bytes: infinities and NaNs go in too.
    fill_bytes(8, 0);
    push_as(k_input_8, 8, format);
    pop_as(k_pop_8, 8, format);
  }
  // Bytes under 0 are integers, in and out.
  fill(1, 4, 48);
  push_as(k_weight_8, 4, INTEGER);
  fill(1, 4, 48);
  push_as(k_input_8, 4, INTEGER);
  pop_as(k_pop_8, 4, INTEGER);

  // A pop to fewer bits rounds by frm: every value frm can hold, small sums and sums past the largest of each kind.
  for (unsigned long mode = 0; mode < 8; mode++)
    for (int scale = 40; scale <= 2047; scale += 2007) {
      push(k_weight_32, 4, 4, scale);
      push(k_input_32, 4, 16, scale);
      push(k_input_32, 4, 16, scale);
      set_frm(mode);
      pop_as(k_pop_16, 8, INTEGER);
      pop_as(k_pop_8, 8, E4M3);
      pop_as(k_pop_8, 8, E5M2);
      pop_as(k_pop_8, 8, INTEGER);
      set_frm(0);
    }
  return 0;
}
