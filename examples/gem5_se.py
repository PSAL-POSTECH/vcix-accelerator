# Test fixture for the examples, not a machine description: gem5's default MinorCPU
# pool plus one accelerator unit. Usage: gem5.opt gem5_se.py --model M.so BINARY
import argparse

import yaml

import m5
from m5.objects import *

parser = argparse.ArgumentParser()
parser.add_argument("binary")
parser.add_argument("--model", required=True)
parser.add_argument("--config", help="machine description (YAML) the model is configured from")
parser.add_argument("--vlen", type=int, default=256)
args = parser.parse_args()


# Top-level scalars of the machine description, each as written in the file:
# BaseLoader keeps them as strings, the same text the Spike adapter hands over.
settings = {}
if args.config:
    with open(args.config) as f:
        document = yaml.load(f, Loader=yaml.BaseLoader)
    settings = {key: value for key, value in document.items() if isinstance(value, str)}


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
event = m5.simulate()
print(f"exit: {event.getCause()} at cycle {m5.curTick() // 1000}")
