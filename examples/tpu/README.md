# tpu

A TPU's units on the two custom opcodes: one model (`libtpu.so`, name `tpu`).
The timing face is four unit classes; each lists its own encodings, `tpu.cc`
owns all of them and hands each call to the unit that owns the instruction.
The functional face is `functional.hpp`: what each instruction does to the
registers and to memory, which `execute` hands every instruction to.

## Units

| Unit | File | Instructions | Timing |
|---|---|---|---|
| `tpu::Sfu` | `sfu.hpp` | `verf`, `vtanh`, `vsin`, `vcos`, `vlog`, `vatan`, `vexp` (`sf.vc.v.iv`, custom-2) | a pipeline: one instruction enters per cycle, the result is ready `tpu_sfu_latency_cycles` after it entered, and any number are inside at once |
| `tpu::Misc` | `misc.hpp` | `vlane_idx`, `compute` (custom-2); the DMA's `dma_config_desc`, `mvin`, `mvin2`, `mvin3`, `mvout` (custom-1) | one cycle each, any number in a cycle |
| `tpu::Systolic` | `systolic.hpp` | the systolic array's input push (`sf.vc.iv` 0), weight push (`sf.vc.iv` 1) and pop (`sf.vc.v.i` 2) (custom-2) | one instruction per cycle. A push of `vl` elements waits for room in the input queue; one element a cycle enters a delay line of 2 x `vpu_num_lanes` - 1 slots and comes out into the output queue; a pop of `vl` waits until the output queue holds `vl`. A weight push changes nothing. The array stops while the output queue is full |
| `tpu::Xlu` | `xlu.hpp` | the cross-lane unit's `xlu_push` (`sf.vc.iv` 3), `xlu_push_pattern` (`sf.vc.ivv` 3) and `xlu_pop` (`sf.vc.v.i` 1) (custom-2), with any field at 19:15 | one instruction per cycle, each one cycle. A push of `vl` adds `vl` to the tile's depth and names the pass by its field at 19:15: [4:3] pre-RPU, [2] XU, [1:0] post-RPU. The first pop after a push starts the pass and is not held; no pop is taken while a pass runs. A push is taken during a pass. A pass takes the sum of its stages that are not 0: 2 x lanes + depth for each RPU, lanes + depth - 1 for the XU |

The systolic array's `compute` has no timing effect and is owned by
`tpu::Misc`. The DMA has no model of its own either: `tpu::Misc` owns its
instructions at one cycle each. Nothing on custom-3 is owned.

`tpu::Xlu` is the `CrossLaneFU` of the gem5 these units came from
(student-Jungmin/gem5 5e4a590872, `src/cpu/minor/func_unit.hh` 395-442 and its
calls in `execute.cc`), which that gem5's CPU config selected with
`TORCHSIM_COMPILE_XLU_FU=1`; the stage costs are that file's and have no
measurement behind them. There a push and a pop took effect when they left the
processor; here they take effect at `issue`, so a pass starts at least one
cycle earlier. As there, a tile taken by one pop holds nothing but the next
tile's first pop, and a push during a pass adds to the depth and renames the
pass that the next pop starts. Without the flag that gem5 had no pass: a push
took 32 cycles with the XU bit and 16 without, a pop 1, and nothing was held.

A unit is a plain copyable class with the methods of the timing face
(`owns`, `configure`, `can_accept`, `issue`, `commit`, `tick`) and a static
`encodings()`, the named encodings `owns` is answered from. `tpu::Sfu`,
`tpu::Systolic` and `tpu::Xlu` change state at `issue`; the array also moves
in `tick`, and a pass of the cross-lane unit runs down there.

## The functional face

`tpu::Functional` does what the Spike these units came from did
(riscv-isa-sim 9f555b4, the units built in), in every lane:

| Instructions | What they do |
|---|---|
| `verf`, `vtanh`, `vsin`, `vcos`, `vlog`, `vatan`, `vexp` | `vd[i] = f(vs2[i])` on halves and singles, and on doubles for `vlog` and `vatan` |
| `vlane_idx` | every element of `vd` is its lane's number, written as 64 bits whatever the element width |
| systolic weight push, input push, pop; `compute` | a weight push adds a column to the matrix, of which a lane keeps its last `lanes` weights; an input push multiplies at once, element `i` of every lane being one input vector; a pop takes the results; `compute` does nothing |
| `xlu_push`, `xlu_push_pattern`, `xlu_pop` | a tile of raw 32-bit values is pushed; the first pop after a push moves it across the lanes as the field at 19:15 of the push says ([4:3] the lanes shuffled before, [2] depth and lane exchanged, [1:0] the lanes shuffled after) |
| `dma_config_desc`, `mvin`, `mvin2`, `mvin3`, `mvout` | a tensor of up to four dimensions between memory and the scratchpad, as the descriptor says: a mask, skip axes, a fill value, a bound on memory, an accumulating `mvout`, indices |

It finds an instruction by the names the units give their encodings, so an
encoding is still stated once. Its state (the matrix and queues of the array,
the tile of the cross-lane unit, the descriptor's address) is in its members,
and is not touched by the timing face.

Where the old Spike trapped, asserted or read past a buffer, the model ends
the run with a line on standard error (`tpu: ...`, exit 1): a special function
on a width it has no form for, a pop of more than was computed, an input push
before any weight, the cross-lane operation 0, a descriptor with a dimension of
0 or one that reaches past its tensor. A transfer past the end of a lane's
scratchpad ends the run with exit 200, as before.

Not carried over: what the old Spike printed under `SPIKE_DEBUG` and
`SPIKE_XLU_DEBUG`, and the list of all-zero tiles it wrote under
`SPIKE_DUMP_SPARSE_TILE`. Halves are rounded to nearest even; the old Spike
used the rounding mode the last floating-point instruction left.

`tests/tpu/functional/run.sh` checks it on Spike: each program prints what its
instructions left, and the checksums in `from-old-spike.sha256` are of what the
old Spike printed for the same program. Given an old Spike as a fourth
argument it also runs it and compares the outputs byte by byte.

## Keys read from the machine description

| Key | Default | Meaning |
|---|---|---|
| `tpu_sfu_latency_cycles` | 10 | cycles from the issue of a special function to its result; at least 1 |
| `tpu_trace` | 0 | not 0: the model prints a line at every issue and every commit |
| `vpu_num_lanes` | 128 | width and height of the systolic array, and the lanes of the cross-lane unit; 1 to 2^20 |
| `tpu_xlu_lanes` | `vpu_num_lanes` | the lanes of the cross-lane unit, when not `vpu_num_lanes`; 1 to 2^20 |
| `tpu_systolic_queue_entries` | 256 | entries in the array's input queue and in its output queue; at least 1 |
| `vpu_spad_size_kb_per_lane` | none | the scratchpad of one lane, which the DMA lays a tensor out over; a transfer without it ends the run |
| `vpu_spad_base_vaddr` | 0xD0000000 | where the scratchpad starts, written `0x...` |
| `run_base_path` | none | not in the file: Spike's `--base-path`. An indirect transfer writes the indices it read to `<path>/indirect_access/indirect_index<n>.raw` |

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
`tests/tpu/run.sh` checks the timing of the four units with them, all on
`libtpu.so`.
