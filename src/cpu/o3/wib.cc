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


}   

    WIB::~WIB()
    {
        dependGraph.reset();
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