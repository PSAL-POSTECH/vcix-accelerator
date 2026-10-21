// FlashAttention of one block of N queries over TILES tiles of N keys, on FSA's N x N array: Q, K and V are halves
// drawn by xorshift (golden.py draws the same), row i element i of every lane; prints O's singles as hex, a row a line.
#include <stdint.h>
#include <stdio.h>

#define LANE_BYTES (512ul * 1024)
#define SPAD_BASE 0xD0000000ul
#define Q_AT 0ul
#define O_AT 65536ul

unsigned long k_attention(void *q, void *o, unsigned long n, unsigned long tiles);

static void *spad(unsigned lane, unsigned long offset) { return (void *)(SPAD_BASE + lane * LANE_BYTES + offset); }
static uint32_t state = 0x2545F491u;
static uint32_t next(void) {
  state ^= state << 13, state ^= state >> 17, state ^= state << 5;
  return state;
}
// A half of sign, exponent 11 to 16 (2^-4 to 2^1) and any fraction.
static uint16_t half(void) {
  const uint32_t r = next();
  return (uint16_t)(((r >> 31) << 15) | ((11 + (r >> 10) % 6) << 10) | (r & 0x3ff));
}

int main(void) {
  for (unsigned row = 0; row < N * (1 + 2 * TILES); row++)
    for (unsigned lane = 0; lane < N; lane++) *(uint16_t *)spad(lane, Q_AT + 2 * row) = half();
  k_attention(spad(0, Q_AT), spad(0, O_AT), N, TILES);
  for (unsigned row = 0; row < N; row++) {
    for (unsigned lane = 0; lane < N; lane++) printf("%08x ", *(uint32_t *)spad(lane, O_AT + 4 * row));
    printf("\n");
  }
  return 0;
}
