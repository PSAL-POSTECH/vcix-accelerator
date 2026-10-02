# Spike adapter

The Spike side lives in the Spike repository, not here: branch `vcix` of
`PSAL-POSTECH/riscv-isa-sim`, on top of the fork whose vector unit has lanes
(`9f555b4`), with that fork's 8-bit floats (`Zvfp8`). `setup/versions.env` pins the commit. It carries no accelerator:
the units that fork had built in (the DMA, the systolic array, the special
functions, `vlane_idx`, the cross-lane unit) are removed, and what they did is
a model's business.

```
spike --extlib=<model.so> --isa=rv64gcv_zfh_xvcixaccel --machine-config=<machine.yml> \
      [--base-path=<dir>] [--kernel-addr=<start>:<end>] pk <program>
```

1. **The model** — `--extlib` loads the library; the extension `vcixaccel`,
   built into Spike and named in the ISA string, finds `vcix_accel_model` in
   it. The run ends, with the library's path and the reason, when no model is
   loaded, when it hands over no table, a table of another ABI version, one
   without a name, or one without `create`, `destroy` or `execute`, and when an
   encoding of the model is not within custom-1 (`0x2B`) or custom-2 (`0x5B`).
   The timing members of the table are not looked at.
2. **Ownership** — the encodings the model lists are registered as the
   extension's instructions. Spike refuses an extension whose encoding
   overlaps an instruction it implements itself, whichever is registered
   first, so a model cannot shadow a base instruction or be shadowed by one.
   An instruction of the two opcodes that the model does not list is an
   illegal instruction.
3. **Instances** — one per hart, made from the machine description when the
   hart first needs it and destroyed with it. A description the model refuses
   ends the run with the model's name and its reason.
4. **Execute** — for custom-2 the vector unit must be on and `vtype` valid,
   and a form that reads `f[rs1]` needs the floating-point unit on; otherwise
   the instruction is illegal and the model is not called. The model is given
   the instruction's bits, `vl`, SEW and LMUL, and a host that reads and
   writes x registers, reads f registers and CSRs, reaches a vector register
   of a lane and reads and writes memory through the MMU, the scratchpad
   included. A CSR is read as the hart holds it, with no check of the
   privilege mode.
   Asking for a vector register to write marks the vector state dirty. A lane,
   a register or a CSR that does not exist ends the run. After `execute`, `vstart`
   is 0: a model never handles it.
5. **The machine description** — `--machine-config` is read by Spike, with
   yaml-cpp, which is in its tree. Spike takes its own machine from it:
   `vpu_num_lanes`, `vpu_spad_size_kb_per_lane` and `vpu_vector_length_bits`
   must be there, `vpu_spad_base_vaddr` may be (default `0xD0000000`). The
   options that set the same things (`--varch`, `--vectorlane-size`,
   `--scratchpad-size`, `--scratchpad-base-vaddr`) are refused beside it.
   Every top-level scalar is what the model's `config.get` answers from.
   A file that is not YAML, or whose top level is not a mapping, is refused.
6. **`--base-path`** — handed to the model as the key `run_base_path`, which
   a machine description may not contain.
7. **The scratchpad** — a buffer of `lanes x size per lane` bytes that the MMU
   serves at its virtual address without translation. It has no physical
   address.

Without `--machine-config` the model is configured with no keys, and Spike
takes its machine from its options.

The branch's own checks are in its `ci-tests/vcix/run.sh`. Its
`riscv/vcix_accel.h` is a copy of `include/vcix_accel.h`; `tests/contract`
compares the two on every run, and the version in the table is checked at
load.
