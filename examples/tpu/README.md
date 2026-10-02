# tpu

The timing face of a TPU's units on the two custom opcodes: one model
(`libtpu.so`, name `tpu`) made of unit classes. `tpu.cc` lists every encoding
the model owns and hands each call to the unit that owns the instruction.

**The functional face is not written yet.** `execute` is empty, so on Spike the
instructions are accepted and compute nothing.

## Units

| Unit | File | Instructions | Timing |
|---|---|---|---|
| `tpu::Sfu` | `sfu.hpp` | `verf`, `vtanh`, `vsin`, `vcos`, `vlog`, `vatan`, `vexp` (`sf.vc.v.iv`, custom-2) | a pipeline: one instruction enters per cycle, the result is ready `tpu_sfu_latency_cycles` after it entered, and any number are inside at once |
| `tpu::Misc` | `misc.hpp` | `vlane_idx`, `compute`, the cross-lane unit's `xlu_push`, `xlu_push_pattern` and `xlu_pop` (custom-2); the DMA's `dma_config_desc`, `mvin`, `mvin2`, `mvin3`, `mvout` (custom-1) | one cycle each, any number in a cycle |

The systolic array's unit is not here yet. Its `compute` has no timing effect
and is owned by `tpu::Misc`.

A unit is a plain copyable class with the methods of the timing face
(`owns`, `configure`, `can_accept`, `issue`, `commit`, `tick`). Its state
changes at `issue`.

## Keys read from the machine description

| Key | Default | Meaning |
|---|---|---|
| `tpu_sfu_latency_cycles` | 10 | cycles from the issue of a special function to its result; at least 1 |
| `tpu_trace` | 0 | not 0: the model prints a line at every issue and every commit |

With `tpu_trace` the model reports, on gem5:

```
[tpu] issue vexp: the first
[tpu] issue vtanh: 10 cycles after the last issue, 0 in flight
[tpu] commit vtanh: 10 cycles after its issue
```

"In flight" counts the instructions issued and not committed. The reports do
not change what the model answers. `tests/tpu/` checks the timing with them.
