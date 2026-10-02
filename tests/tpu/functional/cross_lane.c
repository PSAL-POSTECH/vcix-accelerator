// The cross-lane unit and vlane_idx. Usage: cross_lane lanes | plain <operation>... | pattern <operation>...
// For each operation, at each element width: tiles of several depths, pushed in one or two pieces, with a
// pattern or without, popped short, exactly and long. Output: what each pop left in every lane.
#include <stdlib.h>

#include "common.h"

#define EACH(sew) \
  kernel k_cross_pop_##sew; \
  kernel k_cross_push_1_##sew, k_cross_push_2_##sew, k_cross_push_4_##sew, k_cross_push_5_##sew, k_cross_push_6_##sew, \
      k_cross_push_8_##sew, k_cross_push_9_##sew, k_cross_push_10_##sew, k_cross_push_12_##sew, k_cross_push_13_##sew, \
      k_cross_push_14_##sew, k_cross_push_16_##sew, k_cross_push_17_##sew, k_cross_push_18_##sew, k_cross_push_20_##sew, \
      k_cross_push_21_##sew, k_cross_push_22_##sew; \
  kernel_pattern k_cross_pattern_1_##sew, k_cross_pattern_2_##sew, k_cross_pattern_4_##sew, k_cross_pattern_5_##sew, \
      k_cross_pattern_6_##sew, k_cross_pattern_8_##sew, k_cross_pattern_9_##sew, k_cross_pattern_10_##sew, \
      k_cross_pattern_12_##sew, k_cross_pattern_13_##sew, k_cross_pattern_14_##sew, k_cross_pattern_16_##sew, \
      k_cross_pattern_17_##sew, k_cross_pattern_18_##sew, k_cross_pattern_20_##sew, k_cross_pattern_21_##sew, \
      k_cross_pattern_22_##sew;
typedef unsigned long kernel_pattern(void *source, void *unused, unsigned long elements, void *pattern);
EACH(8) EACH(16) EACH(32)
kernel_pattern k_lane_index_8, k_lane_index_16, k_lane_index_32, k_lane_index_64;

#define ROW(operation, sew) [operation] = {k_cross_push_##operation##_##sew, k_cross_pattern_##operation##_##sew}
#define TABLE(sew) \
  {ROW(1, sew), ROW(2, sew), ROW(4, sew), ROW(5, sew), ROW(6, sew), ROW(8, sew), ROW(9, sew), ROW(10, sew), ROW(12, sew), \
   ROW(13, sew), ROW(14, sew), ROW(16, sew), ROW(17, sew), ROW(18, sew), ROW(20, sew), ROW(21, sew), ROW(22, sew)}
static const struct {
  kernel *plain;
  kernel_pattern *pattern;
} PUSH[3][23] = {TABLE(8), TABLE(16), TABLE(32)};
static kernel *const POP[3] = {k_cross_pop_8, k_cross_pop_16, k_cross_pop_32};

#define IN 0ul
#define PATTERN 4096ul
#define OUT 8192ul

static void push(unsigned width, unsigned operation, int with_pattern, unsigned long count) {
  const unsigned bytes = 1u << width;
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < count; i++) {
      const uint32_t value = random32();
      memcpy(spad(lane, IN + i * bytes), &value, bytes);
      // A lane number, now and then one that is not a lane.
      *(uint32_t *)spad(lane, PATTERN + 4 * i) = random_below(LANES + 2);
    }
  const unsigned long done = with_pattern ? PUSH[width][operation].pattern(spad(0, IN), 0, count, spad(0, PATTERN))
                                          : PUSH[width][operation].plain(spad(0, IN), 0, count);
  if (done != count) puts("a push did not take every element");
}
static void pop(unsigned width, unsigned long count) {
  for (unsigned lane = 0; lane < LANES; lane++) memset(spad(lane, OUT), 0xA5, 64);
  if (POP[width](0, spad(0, OUT), count) != count) puts("a pop did not take every element");
  emit_lanes(OUT, 64);
}

static void lane_index(kernel_pattern *function, unsigned long count) {
  for (unsigned lane = 0; lane < LANES; lane++) memset(spad(lane, OUT), 0xA5, 256);
  function(0, spad(0, OUT), count, (void *)32ul);
  emit_lanes(OUT, 256);
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "lanes")) {
    kernel_pattern *const at[] = {k_lane_index_8, k_lane_index_16, k_lane_index_32, k_lane_index_64};
    for (unsigned width = 0; width < 4; width++) {
      lane_index(at[width], 3);
      lane_index(at[width], 32u >> width);
    }
    return 0;
  }
  if (argc < 3 || (strcmp(argv[1], "plain") && strcmp(argv[1], "pattern"))) {
    puts("usage: cross_lane lanes | plain <operation>... | pattern <operation>...");
    return 1;
  }
  const int with_pattern = !strcmp(argv[1], "pattern");
  for (int argument = 2; argument < argc; argument++) {
    const unsigned operation = (unsigned)strtoul(argv[argument], 0, 10);
    if (operation > 22 || !PUSH[0][operation].plain) {
      printf("%s is not an operation of the unit\n", argv[argument]);
      return 1;
    }
    for (unsigned width = 0; width < 3; width++) {
      for (unsigned long depth = 1; depth <= 6; depth++) {
        push(width, operation, with_pattern, depth);
        pop(width, depth);
      }
      // Two pieces, then a pop that takes less than there is, and one that asks for more than is left.
      push(width, operation, with_pattern, 3);
      push(width, operation, with_pattern, 2);
      pop(width, 2);
      pop(width, 8);
      // A tile replaces what the last pop left.
      push(width, operation, with_pattern, 4);
      pop(width, 1);
      push(width, operation, with_pattern, 2);
      pop(width, 6);
    }
  }
  return 0;
}
