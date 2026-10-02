# tpu

The timing face of a TPU's units on the two custom opcodes: one model
(`libtpu.so`, name `tpu`) made of three unit classes. Each unit lists its own
encodings; `tpu.cc` owns all of them and hands each call to the unit that owns
the instruction.

**The functional face is not written yet.** `execute` is empty, so on Spike the
instructions are accepted and compute nothing.

## Units

| Unit | File | Instructions | Timing |
|---|---|---|---|
| `tpu::Sfu` | `sfu.hpp` | `verf`, `vtanh`, `vsin`, `vcos`, `vlog`, `vatan`, `vexp` (`sf.vc.v.iv`, custom-2) | a pipeline: one instruction enters per cycle, the result is ready `tpu_sfu_latency_cycles` after it entered, and any number are inside at once |
| `tpu::Misc` | `misc.hpp` | `vlane_idx`, `compute`, the cross-lane unit's `xlu_push`, `xlu_push_pattern` and `xlu_pop` (custom-2); the DMA's `dma_config_desc`, `mvin`, `mvin2`, `mvin3`, `mvout` (custom-1) | one cycle each, any number in a cycle |
| `tpu::Systolic` | `systolic.hpp` | the systolic array's input push (`sf.vc.iv` 0), weight push (`sf.vc.iv` 1) and pop (`sf.vc.v.i` 2) (custom-2) | one instruction per cycle. A push of `vl` elements waits for room in the input queue; one element a cycle enters a delay line of 2 x `vpu_num_lanes` - 1 slots and comes out into the output queue; a pop of `vl` waits until the output queue holds `vl`. A weight push changes nothing. The array stops while the output queue is full |

The systolic array's `compute` has no timing effect and is owned by
`tpu::Misc`. The cross-lane unit and the DMA have no model of their own
either: `tpu::Misc` owns their instructions at one cycle each. Nothing on
custom-3 is owned.

A unit is a plain copyable class with the methods of the timing face
(`owns`, `configure`, `can_accept`, `issue`, `commit`, `tick`) and a static
`encodings()`, the named encodings `owns` is answered from. `tpu::Sfu` and
`tpu::Systolic` change state at `issue`; the array also moves in `tick`.

## Keys read from the machine description

| Key | Default | Meaning |
|---|---|---|
| `tpu_sfu_latency_cycles` | 10 | cycles from the issue of a special function to its result; at least 1 |
| `tpu_trace` | 0 | not 0: the model prints a line at every issue and every commit |
| `vpu_num_lanes` | 128 | width and height of the systolic array; 1 to 2^20 |
| `tpu_systolic_queue_entries` | 256 | entries in the array's input queue and in its output queue; at least 1 |

With `tpu_trace` the model reports, on gem5:

```
[tpu] issue vexp: the first
[tpu] issue vtanh: 10 cycles after the last issue, 0 in flight
[tpu] commit vtanh: 10 cycles after its issue
[tpu] issue systolic pop: 11 cycles after the last issue, 0 in flight, input queue 0, output queue 4
```

"In flight" counts the instructions issued and not committed. The issue of a
systolic instruction also reports the entries it found in the array's input
queue and output queue. The reports do not change what the model answers.
`tests/tpu/run.sh` checks the timing of the three units with them, all on
`libtpu.so`.
