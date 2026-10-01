# gem5 adapter

The gem5 side lives in the gem5 repository, not here: branch `vcix` of
`PSAL-POSTECH/gem5`, on top of upstream gem5 `stable` (25.1.0.1,
`f5c5a6e390`). It carries no accelerator.

1. **Decode** — the whole custom-2 opcode decodes to one instruction class,
   `Vcix` (`arch/riscv/insts/vcix.hh`). It computes no values.
2. **Operands** — `Vcix` declares its registers from the VCIX encoding, so gem5
   tracks dependencies through it: `vm` says whether `vd` is written,
   `funct6[5:2]` whether `vs2` and `vd` are read, `funct3` whether the `rs1`
   field is a vector, integer or floating-point register or an immediate. A
   vector operand is a group of LMUL registers; the widening forms use a `vd`
   group twice as large.
3. **OpClass** — `VcixAccel`, for every such instruction.
4. **Functional unit** — `MinorVcixAccelFU`, whose `vcixModel` parameter names
   the model `.so`. `vcixConfigKeys` / `vcixConfigValues` carry the machine
   description; the config script reads the YAML, so gem5 itself parses no
   file. The model is loaded and configured when the CPU is built.
5. **Issue** — if the unit's model owns the instruction, `Execute::issue` asks
   `can_accept`, and a refusal stalls it; on acceptance the result is ready
   after the cycles `latency` returns. An instruction no model owns passes
   through unasked.
6. **Commit** — an owned instruction calls the model's `commit`. An instruction
   no model owns becomes an illegal instruction here, on the same path as any
   other illegal instruction.

The model receives the instruction bits with `vl`, SEW and LMUL.

**custom-1** goes to the model the same way, through a second instruction
class. That opcode has no operand rule of its own, so the instruction is taken
to be R-type on integer registers: it reads `x[rs1]` and `x[rs2]` and writes
nothing.

The branch also carries two changes that are not about accelerators: `SpmXBar`,
a zero-latency bus for scratchpad memory, and a MinorCPU change so that the
memory issue limit holds back only memory references.

A CPU config adds one `MinorVcixAccelFU` to its functional unit pool; nothing
else about the machine changes. The model is the same `.so` the Spike adapter
loads. A config with no such unit cannot run these instructions at all, as with
any OpClass that has no functional unit.

Limits: only MinorCPU asks the model; on other CPU models a VCIX instruction
does nothing. `cpu/minor/execute.cc` includes RISC-V headers for the fault, so
the branch is not ISA-neutral.
