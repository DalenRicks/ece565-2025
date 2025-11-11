#ifndef __CPU_O3_WIB_HH__
#define __CPU_O3_WIB_HH__

// Standard c++ includes
#include <list>
#include <vector>
#include <map>
#include <queue>
#include <string>

// gem5 includes
#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/op_class.hh"
#include "cpu/timebuf.hh"
#include "cpu/o3/comm.hh"
#include "cpu/o3/dep_graph.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "cpu/o3/inst_queue.hh"
#include "cpu/o3/limits.hh"
#include "cpu/o3/lsq.hh"
#include "cpu/o3/mem_dep_unit.hh"
#include "cpu/o3/store_set.hh"
#include "enums/SMTQueuePolicy.hh"
#include "sim/eventq.hh"
#include "sim/probe/probe.hh"


namespace gem5
{

namespace o3
{
class FUPool;
class CPU;
class IEW;

class WIB
{
  public:
    /** Constructs a WIB. */
    WIB(CPU *cpu_ptr, IEW *iew_ptr,
        const BaseO3CPUParams &params);


    /** Destructs a WIB. */
    ~WIB();

    /** Returns the name of the IQ. */
    std::string name() const;

    /** Registers probes. */
    void regProbePoints();

    /** Removes instructions from Issue Queue */
    void removeInstsFromIQ(const DynInstPtr &waiting_inst);
    
    /** Adds instructions to Issue Queue */
    void addInstsToIQ(const DynInstPtr &ready_inst);

    /** Wakes all dependents of a completed instruction. */
    int wakeDependents(const DynInstPtr &completed_inst);



  private:
    /** Pointer to the CPU. */
    CPU *cpu;

    /** Instruction queue. */
    InstructionQueue instQueue;

    /** Load / store queue. */
    LSQ ldstQueue;

    /** To probe when instruction execution is complete. */
    ProbePointArg<DynInstPtr> *ppToCommit;

    //////////////////////////////////////
    // Instruction lists, ready queues, and ordering
    //////////////////////////////////////

    /** List of all the instructions in the WIB (some of which may be issued). */
    std::list<DynInstPtr> instList[MaxThreads];

    /** List of instructions that are ready to be executed. */
    std::list<DynInstPtr> instsToExecute;

    /** List of instructions waiting for their DTB translation to
     *  complete (hw page table walk in progress).
     */
    std::list<DynInstPtr> deferredMemInsts;

    //////////////////////////////////////
    // Various parameters
    //////////////////////////////////////

    /** The number of entries in the instruction queue. */
    unsigned numEntries;

    /** The total number of instructions that can be issued in one cycle. */
    unsigned totalWidth;

    /** The number of physical registers in the CPU. */
    unsigned numPhysRegs;

    DependencyGraph<DynInstPtr> dependGraph;


    struct WIBStats : public statistics::Group
    {
        WIBStats(CPU *cpu, const unsigned &total_width);
        
        /** Stat for number of instructions added. */
        statistics::Scalar instsAdded;

    } wibStats; 
};
} // namespace o3
} // namespace gem5

#endif //__CPU_O3_WIB_HH__