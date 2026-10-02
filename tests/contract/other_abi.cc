// Test model: a table of another ABI version. Nothing but the version is
// filled in, so a caller that reads past it calls a null pointer.
#include "vcix_accel.h"

extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) {
  static const vcix_model table = {VCIX_ACCEL_ABI_VERSION + 1};
  return &table;
}
