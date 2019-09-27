#include "Options.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;
using namespace std;

static cl::OptionCategory ParcoachCategory("Parcoach options");

static cl::opt<bool> clOptDumpSSA("dump-ssa",
                                  cl::desc("Dump the all-inclusive SSA"),
                                  cl::cat(ParcoachCategory));

static cl::opt<string> clOptDumpSSAFunc("dump-ssa-func",
                                        cl::desc("Dump the all-inclusive SSA "
                                                 "for a particular function."),
                                        cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptDotGraph("dot-depgraph",
                  cl::desc("Dot the dependency graph to dg.dot"),
                  cl::cat(ParcoachCategory));

static cl::opt<bool> clOptDumpRegions("dump-regions",
                                      cl::desc("Dump the regions found by the "
                                               "Andersen PTA"),
                                      cl::cat(ParcoachCategory));

static cl::opt<bool> clOptDumpModRef("dump-modref",
                                     cl::desc("Dump the mod/ref analysis"),
                                     cl::cat(ParcoachCategory));

static cl::opt<bool> clOptTimeStats("timer", cl::desc("Print timers"),
                                    cl::cat(ParcoachCategory));

static cl::opt<bool> clOptDotTaintPaths("dot-taint-paths",
                                        cl::desc("Dot taint path of each "
                                                 "conditions of tainted "
                                                 "collectives."),
                                        cl::cat(ParcoachCategory));

static cl::opt<bool> clOptStats("statistics", cl::desc("print statistics"),
                                cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptWithRegName("with-reg-name",
                     cl::desc("Compute human readable names of regions"),
                     cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptContextInsensitive("context-insensitive",
                            cl::desc("Context insensitive version of "
                                     "flooding."),
                            cl::cat(ParcoachCategory));

static cl::opt<bool> clOptInstrumInter(
    "instrum-inter", cl::desc("Instrument code with inter-procedural parcoach"),
    cl::cat(ParcoachCategory));
static cl::opt<bool> clOptInstrumIntra(
    "instrum-intra", cl::desc("Instrument code with intra-procedural parcoach"),
    cl::cat(ParcoachCategory));

static cl::opt<bool> clOptWeakUpdate("weak-update", cl::desc("Weak update"),
                                     cl::cat(ParcoachCategory));
static cl::opt<bool> clOptNoDataFlow("no-dataflow",
                                     cl::desc("Disable dataflow analysis"),
                                     cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptOmpTaint("check-omp", cl::desc("enable OpenMP collectives checking"),
                  cl::cat(ParcoachCategory));
static cl::opt<bool> clOptMpiTaint("check-mpi",
                                   cl::desc("enable MPI collectives checking"),
                                   cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptCudaTaint("check-cuda", cl::desc("enable CUDA collectives checking"),
                   cl::cat(ParcoachCategory));

static cl::opt<bool> clOptUpcTaint("check-upc",
                                   cl::desc("enable UPC collectives checking"),
                                   cl::cat(ParcoachCategory));

static cl::opt<bool>
    clOptInterOnly("inter-only",
                   cl::desc("enable only parcoach interprocedural"),
                   cl::cat(ParcoachCategory));
static cl::opt<bool>
    clOptIntraOnly("intra-only",
                   cl::desc("enable only parcoach intraprocedural"),
                   cl::cat(ParcoachCategory));

static cl::opt<bool> clOptDGUIDA("dg-uida",
                                 cl::desc("use dep graph from paper "
                                          "User-input dependence analysis via "
                                          "graph reachability"),
                                 cl::cat(ParcoachCategory));

static cl::opt<bool> clOptSVF("SVF",
                              cl::desc("use dep graph from paper "
                                       "SVF: Interprocedural Static Value-flow "
                                       "Analysis in LLVM."),
                              cl::cat(ParcoachCategory));
static cl::opt<bool> clOptCompareAll("compare-all",
                                     cl::desc("compare DCF with SVF and UIDA."),
                                     cl::cat(ParcoachCategory));

bool optDumpSSA;
string optDumpSSAFunc;
bool optDotGraph;
bool optDumpRegions;
bool optDumpModRef;
bool optTimeStats;
bool optDotTaintPaths;
bool optStats;
bool optWithRegName;
bool optContextInsensitive;
bool optInstrumInter;
bool optInstrumIntra;
bool optWeakUpdate;
bool optNoDataFlow;
bool optOmpTaint;
bool optCudaTaint;
bool optMpiTaint;
bool optUpcTaint;
bool optInterOnly;
bool optIntraOnly;
bool optDGUIDA;
bool optDGSVF;
bool optCompareAll;

void getOptions() {
  optDumpSSA = clOptDumpSSA;
  optDumpSSAFunc = clOptDumpSSAFunc;
  optDotGraph = clOptDotGraph;
  optDumpRegions = clOptDumpRegions;
  optDumpModRef = clOptDumpModRef;
  optTimeStats = clOptTimeStats;
  optDotTaintPaths = clOptDotTaintPaths;
  optStats = clOptStats;
  optWithRegName = clOptWithRegName;
  optContextInsensitive = clOptContextInsensitive;
  optInstrumIntra = clOptInstrumIntra;
  optInstrumInter = clOptInstrumInter;
  optWeakUpdate = clOptWeakUpdate;
  optNoDataFlow = clOptNoDataFlow;
  optOmpTaint = clOptOmpTaint;
  optCudaTaint = clOptCudaTaint;
  optMpiTaint = clOptMpiTaint;
  optUpcTaint = clOptUpcTaint;
  optInterOnly = clOptInterOnly;
  optIntraOnly = clOptIntraOnly;
  optDGUIDA = clOptDGUIDA;
  optDGSVF = clOptSVF;
  optCompareAll = clOptCompareAll;

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
