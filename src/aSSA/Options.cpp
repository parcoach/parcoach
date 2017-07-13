#include "Options.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;
using namespace std;

static cl::OptionCategory ParcoachCategory("Parcoach options");

static
cl::opt<bool> clOptDumpSSA("dump-ssa",
			   cl::desc("Dump the all-inclusive SSA"),
			   cl::cat(ParcoachCategory));

static
cl::opt<string> clOptDumpSSAFunc("dump-ssa-func",
				 cl::desc("Dump the all-inclusive SSA " \
					  "for a particular function."),
				 cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptDotGraph("dot-depgraph",
			    cl::desc("Dot the dependency graph to dg.dot"),
			    cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptDumpRegions("dump-regions",
			       cl::desc("Dump the regions found by the " \
					"Andersen PTA"),
			       cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptDumpModRef("dump-modref",
			      cl::desc("Dump the mod/ref analysis"),
			      cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptTimeStats("timer",
			     cl::desc("Print timers"),
			     cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptDisablePhiElim("disable-phi-elim",
				  cl::desc("Disable Phi elimination pass"),
				  cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptDotTaintPaths("dot-taint-paths",
				 cl::desc("Dot taint path of each " \
					  "conditions of tainted "	\
					  "collectives."),
				 cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptStats("statistics", cl::desc("print statistics"),
			 cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptNoRegName("no-reg-name",
			     cl::desc("Do not compute names of regions"),
			     cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptContextSensitive("context-sensitive",
				    cl::desc("Context sensitive version of " \
					     "flooding."),
				    cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptInstrumInter("instrum-inter",
				cl::desc("Instrument code with inter-procedural parcoach"),
				cl::cat(ParcoachCategory));
static
cl::opt<bool> clOptInstrumIntra("instrum-intra",
				cl::desc("Instrument code with intra-procedural parcoach"),
				cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptStrongUpdate("strong-update",
				cl::desc("Strong update"),
				cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptNoPtrDep("no-ptr-dep",
			    cl::desc("No dependency with pointer for " \
				     "load/store"),
			    cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptNoPred("no-phi-pred",
			  cl::desc("No dependency with phi predicatesd"),
			  cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptNoDataFlow("no-dataflow",
			      cl::desc("Disable dataflow analysis"),
			      cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptOmpTaint("check-omp",
			    cl::desc("enable OpenMP collectives checking"),
			    cl::cat(ParcoachCategory));
static
cl::opt<bool> clOptMpiTaint("check-mpi",
			    cl::desc("enable MPI collectives checking"),
			    cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptCudaTaint("check-cuda",
			     cl::desc("enable CUDA collectives checking"),
			     cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptUpcTaint("check-upc",
			    cl::desc("enable UPC collectives checking"),
			    cl::cat(ParcoachCategory));

static
cl::opt<bool> clOptInterOnly("inter-only",
			    cl::desc("enable only parcoach interprocedural"),
			    cl::cat(ParcoachCategory));
static
cl::opt<bool> clOptIntraOnly("intra-only",
			    cl::desc("enable only parcoach intraprocedural"),
			    cl::cat(ParcoachCategory));

bool optDumpSSA;
string optDumpSSAFunc;
bool optDotGraph;
bool optDumpRegions;
bool optDumpModRef;
bool optTimeStats;
bool optDisablePhiElim;
bool optDotTaintPaths;
bool optStats;
bool optNoRegName;
bool optContextSensitive;
bool optInstrumInter;
bool optInstrumIntra;
bool optStrongUpdate;
bool optNoPtrDep;
bool optNoPred;
bool optNoDataFlow;
bool optOmpTaint;
bool optCudaTaint;
bool optMpiTaint;
bool optUpcTaint;
bool optInterOnly;
bool optIntraOnly;

void getOptions()
{
  optDumpSSA = clOptDumpSSA;
  optDumpSSAFunc = clOptDumpSSAFunc;
  optDotGraph = clOptDotGraph;
  optDumpRegions = clOptDumpRegions;
  optDumpModRef = clOptDumpModRef;
  optTimeStats = clOptTimeStats;
  optDisablePhiElim = clOptDisablePhiElim;
  optDotTaintPaths = clOptDotTaintPaths;
  optStats = clOptStats;
  optNoRegName = clOptNoRegName;
  optContextSensitive = clOptContextSensitive;
  optInstrumIntra = clOptInstrumIntra;
  optInstrumInter = clOptInstrumInter;
  optStrongUpdate = clOptStrongUpdate;
  optNoPtrDep = clOptNoPtrDep;
  optNoPred = clOptNoPred;
  optNoDataFlow = clOptNoDataFlow;
  optOmpTaint = clOptOmpTaint;
  optCudaTaint = clOptCudaTaint;
  optMpiTaint = clOptMpiTaint;
  optUpcTaint = clOptUpcTaint;
  optInterOnly = clOptInterOnly;
  optIntraOnly = clOptIntraOnly;

  if (optInstrumInter && optInstrumIntra) {
    errs() << "Error: cannot instrument for both intra- and inter- procedural "
	   << "analyses.\n";
    exit(0);
  }

  if (optInstrumIntra && optInterOnly) {
    errs() << "Error: cannot instrument intra-procedural with option "
	   << "-inter-only\n";
    exit(0);

  }

  if (optInstrumInter && optIntraOnly) {
    errs() << "Error: cannot instrument inter-procedural with option "
	   << "-intra-only\n";
    exit(0);

  }
}
