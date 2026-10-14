# The tpu machine's CPU and its functional unit pool: PyTorchSim's gem5_script/vpu_config.py on the gem5 of branch vcix.
import m5
from m5.objects import *

class MinorFPUnit(MinorFU):
    opClasses = minorMakeOpClassSet(
        [
            "FloatAdd",
            "FloatCmp",
            "FloatCvt",
            "FloatMult",
            "FloatMultAcc",
            "FloatDiv",
            "FloatMisc",
            "FloatSqrt",
            "Bf16Cvt",
        ]
    )

# The v6e's VALU: 4 slots a bundle (XLA's static per-bundle utilization, examples/tpu/README.md: MXU 2, XLU 2,
# VALU 4, ...), so 4 units take every vector ALU op class, the adder's, multiplier's, divider's and misc's (vmv,
# vslide, vrgather, cvt, merge, sgnj) together: a mixed stream issues 4 a cycle, not 4 of each kind. Each kept opLat 1.
class MinorVALU(MinorFU):
    opClasses = minorMakeOpClassSet(
        [
            "SimdAdd",
            "SimdFloatAdd",
            "SimdFloatAlu",
            "SimdFloatCmp",
            "SimdShift",
            "SimdShiftAcc",
            "SimdAddAcc",
            "SimdAlu",
            "SimdCmp",
            "SimdBf16Add",
            "SimdBf16Cmp",
            "SimdMult",
            "SimdFloatMult",
            "SimdMultAcc",
            "SimdMatMultAcc",
            "SimdSqrt",
            "SimdFloatMultAcc",
            "SimdFloatMatMultAcc",
            "SimdFloatSqrt",
            "SimdDotProd",
            "SimdBf16Mult",
            "SimdBf16MultAcc",
            "SimdBf16MatMultAcc",
            "SimdBf16DotProd",
            "SimdDiv",
            "SimdFloatDiv",
            "SimdCvt",
            "SimdFloatCvt",
            "SimdFloatMisc",
            "SimdPredAlu",
            "SimdMisc",
            "SimdExt",
            "SimdFloatExt",
            "SimdBf16Cvt",
        ]
    )
    opLat = 1
    unit = "Vpu"

class MinorVecReduce(MinorFU):
    opClasses = minorMakeOpClassSet(
        [
            "SimdReduceAdd",
            "SimdReduceAlu",
            "SimdReduceCmp",
            "SimdFloatReduceAdd",
            "SimdFloatReduceCmp",
        ]
    )
    opLat = 1
    unit = "Vpu"

class MinorVecLdStore(MinorFU):
    opClasses = minorMakeOpClassSet(
        [
            "SimdUnitStrideLoad",
            "SimdUnitStrideStore",
            "SimdUnitStrideMaskLoad",
            "SimdUnitStrideMaskStore",
            "SimdStridedLoad",
            "SimdStridedStore",
            "SimdIndexedLoad",
            "SimdIndexedStore",
            "SimdUnitStrideFaultOnlyFirstLoad",
            "SimdWholeRegisterLoad",
            "SimdWholeRegisterStore",
            "SimdUnitStrideSegmentedLoad",
            "SimdUnitStrideSegmentedStore",
            "SimdUnitStrideSegmentedFaultOnlyFirstLoad",
            "SimdStrideSegmentedLoad",
            "SimdStrideSegmentedStore",
        ]
    )
    opLat = 1

class MinorVecConfig(MinorFU):
    opClasses = minorMakeOpClassSet(
        [
            "SimdConfig",
        ]
    )
    opLat = 1

class MinorCustomIntFU(MinorDefaultIntFU):
    opLat = 1

class MinorCustomIntDivFU(MinorDefaultIntDivFU):
    opLat = 1

class MinorCustomIntMulFU(MinorDefaultIntMulFU):
    opLat = 1

class MinorCustomPredFU(MinorDefaultPredFU):
    opLat = 1

class MinorCustomMemFU(MinorDefaultMemFU):
    opLat = 1

class MinorCustomMiscFU(MinorDefaultMiscFU):
    opLat = 1

class MinorCustomFUPool(MinorFUPool):
    funcUnits = [
        # Scalar unit
        MinorFPUnit(),
        MinorCustomIntFU(),
        MinorCustomIntFU(),
        MinorCustomIntMulFU(),
        MinorCustomIntDivFU(),
        MinorCustomPredFU(),
        MinorCustomMemFU(),
        MinorCustomMiscFU(),

        # Scalar unit
        MinorFPUnit(),
        MinorCustomIntFU(),
        MinorCustomIntFU(),
        MinorCustomIntMulFU(),
        MinorCustomIntDivFU(),
        MinorCustomPredFU(),
        MinorCustomMemFU(),
        MinorCustomMiscFU(),

        # Accelerator: the script names its model
        MinorVcixAccelFU(),

        # Vector
        MinorVecConfig(),
        MinorVecConfig(),
        MinorVecLdStore(),
        MinorVecLdStore(),

        # Vector ALU0
        MinorVALU(),
        MinorVecReduce(),

        # Vector ALU1
        MinorVALU(),
        MinorVecReduce(),

        # Vector
        MinorVecConfig(),
        MinorVecConfig(),
        MinorVecLdStore(),
        MinorVecLdStore(),

        # Vector ALU0
        MinorVALU(),
        MinorVecReduce(),

        # Vector ALU1
        MinorVALU(),
        MinorVecReduce(),

        # A fifth and a sixth integer unit, which the original does not have: a compensation for RVV loads having no
        # immediate offset, not a fact of the v6e (examples/tpu/README.md, "Two more integer units"). Last, so that
        # every unit before them keeps its index in the statistics (funcUnits16 is the accelerator).
        MinorCustomIntFU(),
        MinorCustomIntFU(),
    ]

class RiscvVPU(RiscvMinorCPU):
    fetch1FetchLimit = 8
    decodeInputWidth = 8
    fetch1ToFetch2BackwardDelay = 0
    fetch2InputBufferSize = 8
    decodeInputBufferSize = 8
    decodeInputWidth = 8
    executeInputBufferSize = 128
    executeInputWidth = 12
    executeIssueLimit = 12
    executeCommitLimit = 12

    # Memory
    executeMemoryIssueLimit = 8
    executeMemoryCommitLimit = 8
    executeMaxAccessesInMemory = 8
    executeLSQMaxStoreBufferStoresPerCycle = 8
    executeLSQTransfersQueueSize = 8
    executeLSQStoreBufferSize = 8
    # Two loads or stores enter the LSQ's request (address translation) queue together; gem5's default and the
    # original's is one.
    executeLSQRequestsQueueSize = 2

    executeFuncUnits = MinorCustomFUPool()
