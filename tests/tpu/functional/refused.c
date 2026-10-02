// What the model cannot carry out, one per run. Usage: refused <which>
#include "common.h"

kernel k_vexp_64, k_pop_32, k_input_32, k_cross_push_0_32;

// One row of four words along the split axis, a word per lane.
static struct {
  int32_t dim_size[4], dim_low[4], dim_high[4];
  uint64_t mm_stride[4], spad_stride[4];
  uint16_t element_size, vlane_stride;
  uint8_t split_axis, unused_117;
  uint16_t flags;
  uint64_t indirect_address;
  uint16_t indirect_stride, indirect_element_size;
  uint8_t indirect_dim, unused_133;
  uint16_t indirect_lanes;
  uint64_t fill, memory_base, memory_bytes;
} row = {.dim_size = {1, 1, 1, 4}, .dim_high = {1, 1, 1, 4}, .mm_stride = {4, 4, 4, 1}, .spad_stride = {4, 4, 4, 1},
         .element_size = 4, .vlane_stride = 1, .split_axis = 3};
static uint32_t memory[4];

#define DMA(funct7, a, b) __asm__ volatile(".insn r 0x2b, 3, " #funct7 ", x0, %0, %1" : : "r"(a), "r"(b) : "memory")

int main(int argc, char **argv) {
  const char *which = argc == 2 ? argv[1] : "";
  if (!strcmp(which, "doubles")) {            // a special function without a form for doubles
    k_vexp_64(spad(0, 0), spad(0, 4096), 1);
  } else if (!strcmp(which, "pop")) {         // a pop of what was never computed
    k_pop_32(0, spad(0, 0), 1);
  } else if (!strcmp(which, "input")) {       // an input with no weights
    k_input_32(spad(0, 0), 0, 1);
  } else if (!strcmp(which, "operation")) {   // the cross-lane operation 0
    k_cross_push_0_32(spad(0, 0), 0, 1);
  } else if (!strcmp(which, "overflow")) {    // a transfer to the end of a lane's scratchpad
    DMA(7, &row, &row);
    DMA(2, memory, spad(0, LANE_BYTES));
  } else if (!strcmp(which, "indices")) {     // an indirect transfer: its indices go under --base-path
    row.flags = 1;
    row.indirect_address = (uint64_t)spad(0, 4096);
    row.indirect_stride = 1;
    row.indirect_element_size = 4;
    DMA(7, &row, &row);
    DMA(2, memory, spad(0, 0));
  } else {
    puts("usage: refused doubles | pop | input | operation | overflow | indices");
    return 2;
  }
  puts("it ran");
  return 0;
}
