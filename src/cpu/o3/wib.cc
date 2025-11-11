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
      totalWidth(params.issueWidth),
      wibStats(cpu, totalWidth)
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

WIB::WIBStats::WIBStats(CPU *cpu, const unsigned &total_width)
    : statistics::Group(cpu),
    ADD_STAT(instsAdded, statistics::units::Count::get(),
             "Number of instructions added to the WIB")
{
    instsAdded
        .prereq(instsAdded);
}

WIB::~WIB()
{
    dependGraph.reset();
}

std::string WIB::name() const
{
    return cpu->name() + ".wib";
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

int WIB::wakeDependents(const DynInstPtr &completed_inst)
{
    int dependents = 0;

    completed_inst->lastWakeDependents = curTick();

    // DPRINTF(IQ, "Waking dependents of completed instruction.\n");

    assert(!completed_inst->isSquashed());

    for (int dest_reg_idx = 0;
         dest_reg_idx < completed_inst->numDestRegs();
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg =
            completed_inst->renamedDestIdx(dest_reg_idx);

        // Special case of uniq or control registers.  They are not
        // handled by the IQ and thus have no dependency graph entry.
        if (dest_reg->isFixedMapping()) {
            // DPRINTF(IQ, "Reg %d [%s] is part of a fix mapping, skipping\n",
            //         dest_reg->index(), dest_reg->className());
            continue;
        }

        // Avoid waking up dependents if the register is pinned
        dest_reg->decrNumPinnedWritesToComplete();
        if (dest_reg->isPinned())
            completed_inst->setPinnedRegsWritten();

        if (dest_reg->getNumPinnedWritesToComplete() != 0) {
            // DPRINTF(IQ, "Reg %d [%s] is pinned, skipping\n",
            //         dest_reg->index(), dest_reg->className());
            continue;
        }

        // DPRINTF(IQ, "Waking any dependents on register %i (%s).\n",
        //         dest_reg->index(),
        //         dest_reg->className());

        //Go through the dependency chain, marking the registers as
        //ready within the waiting instructions.
        DynInstPtr dep_inst = dependGraph.pop(dest_reg->flatIndex());

        while (dep_inst) {
            // DPRINTF(IQ, "Waking up a dependent instruction, [sn:%llu] "
            //         "PC %s.\n", dep_inst->seqNum, dep_inst->pcState());

            // Might want to give more information to the instruction
            // so that it knows which of its source registers is
            // ready.  However that would mean that the dependency
            // graph entries would need to hold the src_reg_idx.
            dep_inst->markSrcRegReady();

            // addIfReady(dep_inst);

            dep_inst = dependGraph.pop(dest_reg->flatIndex());

            ++dependents;
        }

        // Reset the head node now that all of its dependents have
        // been woken up.
        assert(dependGraph.empty(dest_reg->flatIndex()));
        dependGraph.clearInst(dest_reg->flatIndex());
    }
    return dependents;
}


} // namespace o3
} // namespace gem5