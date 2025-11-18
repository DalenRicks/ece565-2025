#include "cpu/o3/wib.hh"

#include <limits>
#include <vector>

#include "base/logging.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/fu_pool.hh"
#include "cpu/o3/limits.hh"
#include "debug/IQ.hh"      // I have a feeling that this include will cause issues later. Find out how to make a WIB version or whether it is necessary
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
      instQueue(cpu_ptr, iew_ptr, params),
      ldstQueue(cpu_ptr, iew_ptr, params),
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

/* Maybe when you remove the instruction from the issue queue, you should say what its dependency is as well
    Either that, or in the issue queue, you should look for the dependency chain in the issue queue and just use this
    function purely as a vehicle to move the instructions from one place to the other
*/
void WIB::insertInWIB(const DynInstPtr &new_inst)
{
    // Make sure the instruction is valid
    assert(new_inst);

    DPRINTF(WIB, "Adding instruction [sn:%llu] PC %s to the WIB.\n",
            new_inst->seqNum, new_inst->pcState());

    assert(freeEntries != 0);

    instList[new_inst->threadNumber].push_back(new_inst);

    --freeEntries;

    new_inst->setWaiting();

    // Look through its source registers (physical regs), and mark any
    // dependencies.
    addLongDependency(new_inst);

    // Have this instruction set itself as the producer of its destination
    // register(s).
    addLongProducer(new_inst);

    // if (new_inst->isMemRef()) {
    //     memDepUnit[new_inst->threadNumber].insert(new_inst);
    // } else {
    //     // Will probably be necessary
    //     addIfReady(new_inst);
    // }

    ++wibStats.instsAdded;
    
    // For tracking the number of instructions per thread. Might be helpful
    // count[new_inst->threadNumber]++;

    assert(freeEntries == (numEntries - countInsts()));
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

/*  When we move dependency chains from the issue queue to the wib, we dont necesarially care
    what the *exact* dependency chain is, rather we care about what long instruction the dependency chain stems from.
    So it might be more effective to link all instructions in a dependency chain to the single load instruction,
    even though they might not be directly dependent on it. That way when the load is completed, all of the instructions
    dependent (directly or indirectly) will be marked to be moved back into the issue queue
    
    
    For example, if the following dependency chain exists in the issue queue:
    load p1 4(r2)   load
    add p3 p1 p2    add -> load
    sub p5 p4 p3    sub -> add

    the dependency chain would be rewritten to the following in the WIB:
    load p1 4(r2)   load
    add p3 p1 p2    add -> load
    sub p5 p4 p3    sub -> load

    This might mean that another function will need to be made that will assign the dependency chain to load and this function
    handles the details on how the assigning occurs.
    */
bool WIB::addLongDependency(const DynInstPtr &new_inst)
{
    // Loop through the instruction's source registers, adding
    // them to the dependency list if they are not ready.
    int8_t total_src_regs = new_inst->numSrcRegs();
    bool return_val = false;

    for (int src_reg_idx = 0;
         src_reg_idx < total_src_regs;
         src_reg_idx++)
    {
        
        // Only add it to the dependency graph if it's not ready.
        if (!new_inst->readySrcIdx(src_reg_idx)) {
            PhysRegIdPtr src_reg = new_inst->renamedSrcIdx(src_reg_idx);

            /* Since we only want to track dependency on long instructions, we only care about the tracking 
            the source register if it is sourced by a long instruction. Otherwise, we can skip to the next 
            source register.     
            */

            if (!dependGraph.empty(src_reg->flatIndex())) {
                continue;
            }

            dependGraph.insert(src_reg->flatIndex(), new_inst);
            return_val = true;
            
            // Could potentially track how many instructions were linked using the WIBStats class


            /* Note: These checks were intentially left out since they were already run in the issue queue.
                The only reason that they would be in this function is in the case that the Issue Queue determined that
                it is a long running instruction. So, unlike the Issue Queue counterpart, this function operates under 
                the assumption that the instruction will not become ready in flight between stages. Additionally, we want
                to link the link the instructions using the physical register, regardless of whether it is a fixed mapping*/

            // Check the IQ's scoreboard to make sure the register
            // hasn't become ready while the instruction was in flight
            // between stages.  Only if it really isn't ready should
            // it be added to the dependency graph.
            // if (src_reg->isFixedMapping()) {
            //     continue;               
            // } else if (!regScoreboard[src_reg->flatIndex()]) {
            //     DPRINTF(IQ, "Instruction PC %s has src reg %i (%s) that "
            //             "is being added to the dependency chain.\n",
            //             new_inst->pcState(), src_reg->index(),
            //             src_reg->className());

            //     dependGraph.insert(src_reg->flatIndex(), new_inst);

            //     // Change the return value to indicate that something
            //     // was added to the dependency graph.
            //     return_val = true;
            // } else {
            //     DPRINTF(IQ, "Instruction PC %s has src reg %i (%s) that "
            //             "became ready before it reached the IQ.\n",
            //             new_inst->pcState(), src_reg->index(),
            //             src_reg->className());
            //     // Mark a register ready within the instruction.
            //     new_inst->markSrcRegReady(src_reg_idx);
            // }
        }
    }

    return return_val;
}

void WIB::addLongProducer(const DynInstPtr &long_inst)
{
    // Nothing really needs to be marked when an instruction becomes
    // the producer of a register's value, but for convenience a ptr
    // to the producing instruction will be placed in the head node of
    // the dependency links.
    int8_t total_dest_regs = long_inst->numDestRegs();

    for (int dest_reg_idx = 0;
         dest_reg_idx < total_dest_regs;
         dest_reg_idx++)
    {
        PhysRegIdPtr dest_reg = long_inst->renamedDestIdx(dest_reg_idx);

        if (!dependGraph.empty(dest_reg->flatIndex())) {
            dependGraph.dump();
            panic("Dependency graph %i (%s) (flat: %i) not empty!",
                  dest_reg->index(), dest_reg->className(),
                  dest_reg->flatIndex());
        }

        dependGraph.setInst(dest_reg->flatIndex(), long_inst);
    }
}

int
InstructionQueue::countInsts()
{
    return numEntries - freeEntries;
}


} // namespace o3
} // namespace gem5