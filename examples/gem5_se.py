# Test fixture for the example and the tests, not a machine description: gem5's default
# MinorCPU pool plus one accelerator unit. Exits with the program's exit code, or 1.
# Usage: gem5.opt gem5_se.py --model M.so [--config machine.yml] [--vlen BITS] [--max-ticks N] BINARY
import argparse
import sys

import yaml

import m5
from m5.objects import *

parser = argparse.ArgumentParser()
parser.add_argument("binary")
parser.add_argument("--model", required=True)
parser.add_argument("--config", help="machine description (YAML) the model is configured from")
parser.add_argument("--vlen", type=int, default=256)
parser.add_argument("--max-ticks", type=int, help="stop the simulation after this many ticks")
args = parser.parse_args()


# The tag PyYAML's resolver gives a YAML null.
YAML_NULL = "tag:yaml.org,2002:null"


# The machine description as vcix_config in include/vcix_accel.h defines it.
# Composed, not loaded: values keep their text, and null is the resolver's decision.
def machine_description(path):
    with open(path) as f:
        root = next(yaml.compose_all(f, Loader=yaml.SafeLoader), None)
    if root is None:
        return {}
    if not isinstance(root, yaml.MappingNode):
        print(f"gem5_se.py: {path}: the top level of a machine description is a mapping", file=sys.stderr)
        sys.exit(1)
    return {
        key.value: value.value
        for key, value in root.value
        if isinstance(key, yaml.ScalarNode) and isinstance(value, yaml.ScalarNode) and value.tag != YAML_NULL
    }


settings = machine_description(args.config) if args.config else {}


class ExampleFUPool(MinorFUPool):
    funcUnits = MinorDefaultFUPool.funcUnits + [
        MinorVcixAccelFU(
            vcixModel=args.model,
            vcixConfigKeys=list(settings.keys()),
            vcixConfigValues=list(settings.values()),
        )
    ]


system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]

system.cpu = RiscvMinorCPU(executeFuncUnits=ExampleFUPool())
system.cpu.ArchISA.vlen = args.vlen

system.membus = SystemXBar()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.cpu.createInterruptController()
system.mem_ctrl = SimpleMemory(range=system.mem_ranges[0], port=system.membus.mem_side_ports)
system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible(args.binary)
system.cpu.workload = Process(cmd=[args.binary])
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
event = m5.simulate(*([args.max_ticks] if args.max_ticks is not None else []))
print(f"exit: {event.getCause()} at cycle {m5.curTick() // 1000}")

# The cause gem5 gives when the program exits.
PROGRAM_EXITED = "exiting with last active thread context"
if event.getCause() != PROGRAM_EXITED:
    print(f"gem5_se.py: the simulation did not end with the program exiting: {event.getCause()}", file=sys.stderr)
    sys.exit(1)
sys.exit(event.getCode())
