#include "cpu/o3/wib.hh"

#include <limits>
#include <vector>

#include "base/logging.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/fu_pool.hh"
#include "cpu/o3/limits.hh"
#include "debug/IQ.hh"
#include "enums/OpClass.hh"
#include "params/BaseO3CPU.hh"
#include "sim/core.hh"
#include "sim/probe/probe.hh"

using std::list;

namespace gem5
{ 
namespace o3
{



WIB::WIB(CPU *cpu_ptr, IEW *iew_ptr,
    const BaseO3CPUParams &params)
    : cpu(cpu_ptr),
      iewStage(iew_ptr),
	  numEntries(params.numWIBEntries),
      totalWidth(params.issueWidth)

{
    const auto &reg_classes = params.isa[0]->regClasses();
    // Set the number of total physical registers
    // As the vector registers have two addressing modes, they are added twice
    numPhysRegs = params.numPhysIntRegs + params.numPhysFloatRegs +
                    params.numPhysVecRegs +
                    params.numPhysVecRegs * (
                            reg_classes.at(VecElemClass).numRegs() /
                            reg_classes.at(VecRegClass).numRegs()) +
                    params.numPhysVecPredRegs +
                    params.numPhysCCRegs;

    //Create an entry for each physical register within the
    //dependency graph.
    dependGraph.resize(numPhysRegs);
}   

WIB::~WIB()
{
    dependGraph.reset();
}

void WIB::regProbePoints()
{
    /**
     * Probe point with dynamic instruction as the argument used to probe when
     * an instruction execution completes and it is marked ready to commit.
     */
    ppToCommit = new ProbePointArg<DynInstPtr>(
        cpu->getProbeManager(), "ToCommit");
}

std::string WIB::name() const
{
    return cpu->name() + ".wib";
}



WIB::WIBStats::WIBStats(CPU *cpu, const unsigned &total_width)
    : statistics::Group(cpu),
    ADD_STAT(instsAdded, statistics::units::Count::get(),
             "Number of instructions added to the WIB")
{
    instsAdded
        .prereq(instsAdded);
}
} // namespace o3
} // namespace gem5