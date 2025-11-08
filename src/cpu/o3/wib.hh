#ifndef __CPU_O3_WIB_HH__
#define __CPU_O3_WIB_HH__

// Standard c++ includes
#include <vector>
#include <string>

// gem5 includes
#include "cpu/o3/dyn_inst_ptr.hh"


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





  private:
    /** Pointer to the CPU. */
    CPU *cpu;

    /** Pointer to IEW stage. */
    IEW *iewStage;

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

    DependencyGraph<DynInstPtr> dependGraph;


    struct WIBStats : public statistics::Group
    {
        WIBStats(CPU *cpu, const unsigned &total_width);
        
        /** Stat for number of instructions added. */
        statistics::Scalar instsAdded;

    } wibStats;

    struct 
};
} // namespace o3
} // namespace gem5

#endif //__CPU_O3_WIB_HH__