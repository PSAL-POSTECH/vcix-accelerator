// Test model: a table of another ABI version, with nothing else filled in.
#include "vcix_accel.h"

extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) {
  static const vcix_model table = {VCIX_ACCEL_ABI_VERSION + 1};
  return &table;
}
