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
| Timing     | gem5      | `can_accept`, `latency`, `tick` (optional) | whether the unit can take the instruction now, and when its result is ready |

Stall and latency are delegated to the model. On the gem5 side the accelerator
is a single custom unit with unit operation and issue latency; the model keeps
whatever internal structure it needs (several sub-units, queues, pipelines)
behind that one unit.

## Constraints the interface is built around

- **The timing face cannot see data.** gem5 does not compute vector values for
  these instructions, so `can_accept` and `latency` may depend only on the
  instruction bits and on state the model tracks itself (for example, how many
  pushes a queue has accepted). Data-dependent latency is out of scope.
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

## Status

Design document only. The interface header, the adapters, and an example model
follow in separate changes.
