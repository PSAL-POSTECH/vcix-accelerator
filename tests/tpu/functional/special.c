// The special functions on every lane: each of the 65536 halves once, 4096 singles per lane, and 1024
// doubles per lane for the two functions that take them. Output: per function and width, each lane's results.
#include "common.h"

kernel k_verf_16, k_vtanh_16, k_vsin_16, k_vcos_16, k_vlog_16, k_vatan_16, k_vexp_16;
kernel k_verf_32, k_vtanh_32, k_vsin_32, k_vcos_32, k_vlog_32, k_vatan_32, k_vexp_32;
kernel k_vlog_64, k_vatan_64;

#define IN 0ul
#define OUT (128ul * 1024)

static void run(kernel *function, unsigned element_bytes, unsigned long count) {
  for (unsigned lane = 0; lane < LANES; lane++) memset(spad(lane, OUT), 0xA5, count * element_bytes);
  for (unsigned long done = 0; done < count;)
    done += function(spad(0, IN + done * element_bytes), spad(0, OUT + done * element_bytes), count - done);
  emit_lanes(OUT, count * element_bytes);
}

// Zeros, ones, the ends of the range, infinities, quiet and signalling NaNs of both signs.
static const uint32_t SINGLES[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0x40000000, 0x40490fdb, 0xc0490fdb,
    0x3fc90fdb, 0x00000001, 0x80000001, 0x007fffff, 0x00800000, 0x7f7fffff, 0xff7fffff, 0x7f800000,
    0xff800000, 0x7fc00000, 0xffc00000, 0x7fa00000, 0xffa00000, 0x7fc12345, 0x42b17218, 0x42b17217,
    0xc2cff1b5, 0xc2cff1b4, 0x41200000, 0xc1200000, 0x3a83126f, 0x447a0000, 0x4b800000, 0xcb800000,
};
static const uint64_t DOUBLES[] = {
    0x0000000000000000, 0x8000000000000000, 0x3ff0000000000000, 0xbff0000000000000, 0x3fe0000000000000,
    0x4000000000000000, 0x400921fb54442d18, 0x0000000000000001, 0x000fffffffffffff, 0x0010000000000000,
    0x7fefffffffffffff, 0xffefffffffffffff, 0x7ff0000000000000, 0xfff0000000000000, 0x7ff8000000000000,
    0xfff8000000000000, 0x7ff4000000000000, 0x4024000000000000, 0xc024000000000000, 0x3f50624dd2f1a9fc,
};

int main(void) {
  const unsigned long halves = 65536 / LANES, singles = 4096, doubles = 1024;

  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < halves; i++) *(uint16_t *)spad(lane, IN + 2 * i) = (uint16_t)(i * LANES + lane);
  kernel *const on_halves[] = {k_verf_16, k_vtanh_16, k_vsin_16, k_vcos_16, k_vlog_16, k_vatan_16, k_vexp_16};
  for (unsigned f = 0; f < 7; f++) run(on_halves[f], 2, halves);

  // A lane starts with the listed values, rotated by its number; then any bits, and values near zero.
  const unsigned listed = sizeof SINGLES / sizeof SINGLES[0];
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < singles; i++) {
      uint32_t bits;
      if (i < listed) {
        bits = SINGLES[(i + lane) % listed];
      } else if (i % 2) {
        bits = random32();
      } else {
        const float value = ((int32_t)random_below(400001) - 200000) / 4096.0f;
        memcpy(&bits, &value, sizeof bits);
      }
      *(uint32_t *)spad(lane, IN + 4 * i) = bits;
    }
  kernel *const on_singles[] = {k_verf_32, k_vtanh_32, k_vsin_32, k_vcos_32, k_vlog_32, k_vatan_32, k_vexp_32};
  for (unsigned f = 0; f < 7; f++) run(on_singles[f], 4, singles);

  const unsigned listed_doubles = sizeof DOUBLES / sizeof DOUBLES[0];
  for (unsigned lane = 0; lane < LANES; lane++)
    for (unsigned long i = 0; i < doubles; i++) {
      uint64_t bits;
      if (i < listed_doubles) {
        bits = DOUBLES[(i + lane) % listed_doubles];
      } else if (i % 2) {
        bits = (uint64_t)random32() << 32 | random32();
      } else {
        const double value = ((int32_t)random_below(400001) - 200000) / 4096.0;
        memcpy(&bits, &value, sizeof bits);
      }
      *(uint64_t *)spad(lane, IN + 8 * i) = bits;
    }
  run(k_vlog_64, 8, doubles);
  run(k_vatan_64, 8, doubles);
  return 0;
}
