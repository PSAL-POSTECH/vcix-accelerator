# vcix-accelerator

A third-party accelerator model interface for RISC-V simulators, built on the
observation that a VCIX coprocessor talks to the core through one channel only:
VCIX instructions.

## Goal

A user who wants to add a new accelerator writes **one shared library** that
describes both what the accelerator computes and how long it takes. They do not
patch Spike, and they do not patch gem5.

Today, adding a unit means editing two simulators by hand, in two different
styles:

- **Spike** (functional): a `DECLARE_INSN` in `encoding.h`, a body in
  `insns/*.h`, and unit state stored as members of `processor_t`.
- **gem5** (timing): a custom `OpClass`, a decoder entry per encoding, a
  functional unit class, and a branch in `Execute::issue` of the MinorCPU.
  The gem5 branch used here starts over from upstream gem5 and carries none of
  that: it knows that VCIX instructions exist, and nothing about any unit.

The two descriptions drift apart. In the tree this work started from, the
cross-lane unit exists in Spike and has no decoder entry in gem5 at all.

## Design

Each simulator is modified **once**, to forward the VCIX opcode space to a
loaded model. After that, a new accelerator is a new `.so`.

```
user model (.so)      includes one header; knows nothing about Spike or gem5
--------------------  C ABI: a struct of function pointers
Spike adapter         one extension that claims the VCIX encodings the model owns
gem5 adapter          one generic decode entry, one OpClass, one functional unit
```

A model has two faces, and each simulator calls only its own:

| Face       | Called by | Entry points                          | Answers                         |
|------------|-----------|---------------------------------------|---------------------------------|
| Functional | Spike     | `execute`                             | what values the instruction produces |
| Timing     | gem5      | `can_accept`, `latency`, `commit`     | whether the unit can take the instruction now, when its result is ready, and what the unit's state is afterwards |

Stall and latency are delegated to the model. On the gem5 side the accelerator
is a single custom unit with unit operation and issue latency; the model keeps
whatever internal structure it needs (several sub-units, queues, pipelines)
behind that one unit.

## Writing your hardware model

There are three kinds of code here. Only the first is yours.

```
+-- you write this, once per accelerator ---------------------------+
|  one model class  ->  one shared library (libyour_model.so)       |
|    owns()         which encodings are mine                        |
|    execute()      what the instruction computes   <- Spike calls  |
|    can_accept()   can the unit take it now        <- gem5 calls   |
|    latency()      cycles until the result is ready <- gem5 calls  |
|    commit()       it ran; update the unit's state  <- gem5 calls  |
+-------------------------------------------------------------------+
            ^ the same .so is loaded by both simulators ^
+-- provided, written once -----+  +-- provided, written once ------+
|  Spike adapter                |  |  gem5 adapter                  |
|  libvcix_spike.so             |  |  branch vcix of the gem5 repo  |
|  (adapters/spike)             |  |  (see adapters/gem5)           |
+-------------------------------+  +--------------------------------+
```

### What you implement

One class derived from `vcix_accel::Model` (`include/vcix_accel.hpp`), built
into one `.so`. It needs neither simulator's source tree.

| Method | Called by | You say |
|---|---|---|
| `name()` | both | the model's name |
| `owns()` | both | the `{match, mask}` encodings that belong to this model |
| `configure(config)` | both | nothing; read the machine's numbers by key (optional) |
| `execute(host, insn)` | Spike | what the instruction does to registers and memory, through `host` |
| `can_accept(insn, now)` | gem5 | whether the unit can start this instruction in this cycle; must not change state |
| `latency(insn, now)` | gem5 | cycles until the result is ready; must not change state |
| `commit(insn, now)` | gem5 | nothing; this is where the timing state changes, once per instruction that really ran |
| `reset()` | both | return to the initial state (optional) |

`insn` is the instruction bits together with the vector configuration it was
decoded under: `vl`, SEW and LMUL. Both faces receive the same thing, so
latency can depend on how much data the instruction moves.

`configure` is called once, before anything else. `config.get("key")` returns
the value of a top-level key of the machine description, as written there, or
nothing if the key is absent. The model never sees the file: the adapters read
it, and both simulators hand the model the same values. This repository defines
no configuration format of its own; the file is the machine description a setup
already has.

Then one line registers it:

```cpp
VCIX_ACCEL_REGISTER(YourModel)
```

`can_accept` and `latency` are asked when gem5 issues the instruction, and an
issued instruction can be squashed and issued again, so they may be called more
than once for an instruction that runs once. Only `commit` is one-to-one with
the program. Between an instruction's issue and its commit, a later instruction
sees the state as it was before the first one.

The model has two sets of methods because the two simulators know different
things: Spike knows values and not time, gem5 knows time and not values. They
live in one class and share `owns()`, but they are still two descriptions, and
both are yours to write.

### What you do not implement

- **Decoding, registration, dispatch.** The adapters register your encodings
  and route matching instructions to your model.
- **Rejecting what you do not support.** `owns()` is the whole definition of
  what the model supports. An instruction it does not list never reaches the
  model and ends as an illegal instruction on both simulators.
- **Anything inside Spike or gem5.** No `encoding.h` entry, no instruction
  header, no `OpClass`, no decoder entry, no functional unit class.
- **The adapters.** They do not change when the accelerator changes.

### What you configure, per run

Not code, only where the model is:

| Simulator | How the adapter is enabled | How the model is named | How the machine description is given |
|---|---|---|---|
| Spike | `--extlib=libvcix_spike.so` and the ISA suffix `_xvcixaccel` | environment variable `VCIX_ACCEL_MODEL=<path>` | environment variable `VCIX_ACCEL_CONFIG=<yaml>`; the adapter reads it |
| gem5 | one `MinorVcixAccelFU` in the CPU's functional unit pool | that unit's `vcixModel = "<path>"` | that unit's `vcixConfigKeys` / `vcixConfigValues`; the config script reads the same YAML |

`MinorVcixAccelFU` is defined in the gem5 branch, beside gem5's own units. A
machine's CPU config stays wherever it already lives and gains that one unit;
this repository keeps no CPU config of its own. `examples/gem5_se.py` is only a
fixture for running the examples: gem5's default pool plus the one unit.

## Constraints the interface is built around

- **The timing face cannot see data.** gem5 does not compute vector values for
  these instructions, so the timing face may depend only on the instruction
  (its bits, `vl`, SEW, LMUL) and on state the model tracks itself in `commit` (for example, how many
  pushes a queue has taken). Data-dependent latency is out of scope.
- **The two faces run in different processes.** A Spike run and a gem5 run share
  nothing at run time. Functional state and timing state live in the same model
  class but must not depend on each other.
- **The boundary is a C ABI.** Spike does not install its headers, and a model
  that subclasses simulator types would have to be built against both simulator
  source trees. A plain C boundary keeps the model independent of both, and
  leaves room for models written in other languages.

## Non-goals

- Co-simulation. Spike and gem5 are not run together; they consume the same
  model separately.
- Modelling memory transfers (DMA) in gem5. Their timing is owned by the
  system-level simulator downstream.
- Any accelerator interface other than VCIX.

## Layout

```
include/vcix_accel.h      the C ABI: vcix_model, vcix_host, vcix_encoding
include/vcix_accel.hpp    C++ wrapper: subclass Model, VCIX_ACCEL_REGISTER
adapters/spike/           the Spike extension `vcixaccel`
adapters/gem5/            where the gem5 change lives
examples/                 models, each with a program that exercises it,
                          and a gem5 fixture to run them
tools/timing_probe.cc     drives a model's timing face without gem5
tests/ownership/          an unowned instruction is illegal on both simulators
```

## Examples

The examples are not a prescribed structure. Each shows one way a model can be
put together on top of the same interface; a real accelerator will pick its own
encodings, state, and timing.

- `print_args` — a unit that only observes. It owns the whole custom-2 and
  custom-1 spaces,
  computes nothing, and prints what each face is given: the operands it can see
  on the functional face, the instruction and the cycle on the timing face.
  An instruction takes a configured number of cycles per register of its
  operand group (so LMUL shows up in the timing), and the unit is busy for 3
  after a commit. `machine.yml` stands in for the machine description. `vcix.S` drives it with one `sf.vc.*`
  instruction of each operand form, one under LMUL=2, and one custom-1
  instruction.

## Build and run

```
cmake -G Ninja -S . -B build -DSPIKE_SRC=<riscv-isa-sim> -DSPIKE_BUILD=<riscv-isa-sim>/build
ninja -C build
examples/print_args/run.sh build <spike> <pk> <gem5.opt>
tests/ownership/run.sh     build <spike> <pk> <gem5.opt>
```

Without `SPIKE_SRC` and `SPIKE_BUILD` only the models and the probe are built;
a model needs no simulator tree.

Spike loads the adapter with `--extlib=libvcix_spike.so` and the ISA string
suffix `_xvcixaccel`, and the adapter loads the model named by the
`VCIX_ACCEL_MODEL` environment variable. gem5 takes the model as the
`vcixModel` parameter of a functional unit.

## Status

- Interface header and C++ wrapper: first draft.
- Spike adapter: working.
- gem5 adapter: working; branch `vcix` of `PSAL-POSTECH/gem5`, on upstream
  gem5 25.1.0.1 (see `adapters/gem5/`).
