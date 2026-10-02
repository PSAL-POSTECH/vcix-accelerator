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

**On gem5 only MinorCPU is supported.** The other CPU models do not ask the
model, and what they do instead is wrong rather than an error: see
`adapters/gem5/README.md`.

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
| `configure(config)` | both | nothing; read the machine's numbers by key (need not be overridden) |
| `execute(host, insn)` | Spike | what the instruction does to registers and memory, through `host` |
| `can_accept(insn, now, pending)` | gem5 | whether the unit can start this instruction in this cycle; must not change state |
| `latency(insn, now, pending)` | gem5 | cycles until the result is ready; must not change state |
| `commit(insn, now)` | gem5 | nothing; this is where the timing state changes, once per instruction that really ran |
| `reset()` | both | return to the state right after `configure` (need not be overridden) |

`insn` is the instruction bits together with the vector configuration it was
decoded under: `vl`, SEW and LMUL. Both faces receive the same thing, so
latency can depend on how much data the instruction moves.

`configure` is called once, before any instruction reaches the model, but not
before `name()` and `owns()`: those are asked when the library is loaded, and
their answers are kept. A model's name and the encodings it owns therefore
cannot depend on the machine description. The name, and the name in each
encoding, must stay valid as long as the library is loaded; string literals do.
What `config.get` returns is valid only until `configure` returns, so a model
copies what it wants to keep.

`config.get("key")` returns
the value of a top-level key of the machine description, as written there, or
nothing if the key is absent. "As written" is the scalar's text with no typing
applied: `010` arrives as the text `010`. "Absent" covers a key the file does
not have, a key whose value is YAML null (`k:`, `k: ~`, `k: null`) and a key
whose value is not a scalar; `k: ""` is present, with empty text. The rule is
stated once, at `vcix_config` in `include/vcix_accel.h`, and
`tests/contract` holds both simulators to it. The model never sees the file: the
adapters read it, and both simulators hand the model the same values. This
repository defines no configuration format of its own; the file is the machine
description a setup already has.

`config.uint("key", fallback)` reads a value as an unsigned decimal number. The
fallback is for an absent key only. A value that is present and is not such a
number -- `abc`, `-1`, `1e3`, `7.9`, `0x10`, `8 cycles`, an empty string, a
number too large for 64 bits, or one with a leading zero such as `010` -- is a
configuration error: the run stops with the model's name, the key and the
value on stderr and exit status 1. It is never read as 0 or replaced by the
fallback. A model that reads a value itself reports a bad one the same way, by
throwing `vcix_accel::ConfigError` from `configure`.

Then one line registers it:

```cpp
VCIX_ACCEL_REGISTER(YourModel)
```

Build the library with hidden visibility (`-fvisibility=hidden
-fvisibility-inlines-hidden`; in CMake, `CXX_VISIBILITY_PRESET hidden`). The
macro exports the one symbol a simulator looks up, `vcix_accel_model`, by
itself, so nothing else has to be marked. A symbol the library leaves visible
is not its own any more:

- Both simulators export their symbols, and theirs win. A model that defined
  `f16_to_f32` with default visibility called the simulator's softfloat
  function instead, on Spike and on gem5.
- A simulator can hold more than one model, and two libraries' visible symbols
  of the same name are merged: two models whose classes were both called
  `Accel` used to answer with one table. The model object and its table are
  now hidden whatever the flags.

The flag covers code compiled with it. A static library linked into the model
that was not built hidden needs `-Wl,-Bsymbolic` on the model's link line as
well. `tests/contract` checks that two same-named models each get their own
table and that a hidden model calls its own `f16_to_f32`.

`can_accept` and `latency` are asked when gem5 issues the instruction, and an
issued instruction can be squashed and issued again, so they may be called more
than once for an instruction that runs once. Only `commit` is one-to-one with
the program, and it is the only place the model's state changes.

An accepted instruction does not hold the unit. It is in flight from its issue
until its commit, `latency` cycles later or when the instructions before it
have committed, whichever is later. `pending` is the list of this model's
instructions in flight, oldest first, each with the cycle it was issued and the
cycle it will be ready; gem5 keeps the list and drops squashed instructions
from it. So how many instructions overlap is the model's decision: a unit that
takes one at a time accepts only when `pending` is empty, a pipelined unit
accepts while `pending` is shorter than its depth, and a unit with a queue
counts the pushes in flight.

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
  leaves room for models written in other languages. `include/vcix_accel.h` is
  the contract for such a model and for an adapter: which members of the table
  may be NULL, how long each string lives, and what the machine description
  hands over are stated there and nowhere else.

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
tests/run.sh              every test below, on both simulators
tests/contract/           the rules of the interface: ownership, the machine
                          description, the model table, and the test harness
tests/pipeline/           a pipelined model overlaps instructions on gem5
tests/print_args/         the example reaches the model once per instruction
setup/                    the pinned environment: versions.env, the scripts
                          that build it, and the image
scripts/                  build this repository; how each simulator is started
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
  operand group (so LMUL shows up in the timing). It takes one instruction at
  a time and is busy for 3 cycles after a commit. `machine.yml` stands in for the machine description. `vcix.S` drives it with one `sf.vc.*`
  instruction of each operand form, one under LMUL=2, and one custom-1
  instruction.

## Environment

Building and running needs a RISC-V toolchain, the proxy kernel, Spike and the
gem5 branch. `setup/versions.env` pins all of them -- repository and commit for
each simulator, the scons and Python that gem5 needs -- and is the only place a
version is written. There are two ways to get what it describes.

**The image.** CI publishes the environment, already built, to GHCR. Its tag is
derived from the contents of `setup/`, so a checkout names the image it needs:

```
docker run --rm -it -v "$PWD":/work -w /work "$(setup/image.sh ref)" bash
```

`setup/image.sh build` builds the same image locally. It holds the toolchain,
pk, Spike (source and build tree, which the adapter is built against) and the
gem5 binary, but nothing of this repository: a checkout is built inside it.

**The scripts**, on Ubuntu 22.04:

```
sudo setup/system.sh       # system packages
setup/setup.sh -j 16       # toolchain, pk, spike, gem5, then this repository
```

Everything lands under `/opt/vcix-env`; set `VCIX_ENV_ROOT` to put it elsewhere
(and keep it set when running). The gem5 build is most of the time: about ten
minutes at `-j 24`. `setup/setup.sh spike repo` runs only those steps.

## Build and run

```
scripts/build.sh                 # cmake + ninja into build/, against the Spike above
tests/run.sh                     # every test, on both simulators; non-zero if any fails
examples/print_args/run.sh       # the example, printing what each face is given
```

The run scripts use the simulators the setup produced. To use others, pass them
-- `tests/run.sh <build-dir> <spike> <pk> <gem5.opt>` -- or set `SPIKE`, `PK`,
`GEM5`; `scripts/build.sh` takes the Spike tree from `SPIKE_ROOT`.

By hand, the build is

```
cmake -G Ninja -S . -B build -DSPIKE_SRC=<riscv-isa-sim> -DSPIKE_BUILD=<riscv-isa-sim>/build
ninja -C build
```

Without `SPIKE_SRC` and `SPIKE_BUILD` only the models and the probe are built;
a model needs no simulator tree.

Spike loads the adapter with `--extlib=libvcix_spike.so` and the ISA string
suffix `_xvcixaccel`, and the adapter loads the model named by the
`VCIX_ACCEL_MODEL` environment variable. gem5 takes the model as the
`vcixModel` parameter of a functional unit. `scripts/sim.sh` is where the run
scripts keep both command lines.

## Status

- Interface header and C++ wrapper: first draft.
- Spike adapter: working.
- gem5 adapter: working, on MinorCPU only; branch `vcix` of
  `PSAL-POSTECH/gem5`, on upstream gem5 25.1.0.1 (see `adapters/gem5/`).
