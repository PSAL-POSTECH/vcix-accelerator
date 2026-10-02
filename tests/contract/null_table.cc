// Not a model: the entry symbol is there and hands over no table.
#include "vcix_accel.h"

extern "C" __attribute__((visibility("default"))) const vcix_model *vcix_accel_model(void) { return nullptr; }
