#include "Options.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;
using namespace std;

static cl::OptionCategory ParcoachCategory("Parcoach options");

static cl::opt<bool, true> clOptDumpSSA("dump-ssa",
                                        cl::desc("Dump the all-inclusive SSA"),
                                        cl::cat(ParcoachCategory),
                                        cl::location(optDumpSSA));

static cl::opt<string, true>
    clOptDumpSSAFunc("dump-ssa-func",
                     cl::desc("Dump the all-inclusive SSA "
                              "for a particular function."),
                     cl::cat(ParcoachCategory), cl::location(optDumpSSAFunc));

static cl::opt<bool, true>
    clOptDotGraph("dot-depgraph",
                  cl::desc("Dot the dependency graph to dg.dot"),
                  cl::cat(ParcoachCategory), cl::location(optDotGraph));

static cl::opt<bool, true>
    clOptDumpRegions("dump-regions",
                     cl::desc("Dump the regions found by the "
                              "Andersen PTA"),
                     cl::cat(ParcoachCategory), cl::location(optDumpRegions));

static cl::opt<bool, true>
    clOptDumpModRef("dump-modref", cl::desc("Dump the mod/ref analysis"),
                    cl::cat(ParcoachCategory), cl::location(optDumpModRef));

static cl::opt<bool, true> clOptTimeStats("timer", cl::desc("Print timers"),
                                          cl::cat(ParcoachCategory),
                                          cl::location(optTimeStats));

static cl::opt<bool, true> clOptDotTaintPaths("dot-taint-paths",
                                              cl::desc("Dot taint path of each "
                                                       "conditions of tainted "
                                                       "collectives."),
                                              cl::cat(ParcoachCategory),
                                              cl::location(optDotTaintPaths));

static cl::opt<bool, true> clOptStats("statistics",
                                      cl::desc("print statistics"),
                                      cl::cat(ParcoachCategory),
                                      cl::location(optStats));

static cl::opt<bool, true>
    clOptWithRegName("with-reg-name",
                     cl::desc("Compute human readable names of regions"),
                     cl::cat(ParcoachCategory), cl::location(optWithRegName));

static cl::opt<bool, true>
    clOptContextInsensitive("context-insensitive",
                            cl::desc("Context insensitive version of "
                                     "flooding."),
                            cl::cat(ParcoachCategory),
                            cl::location(optContextInsensitive));

static cl::opt<bool, true> clOptInstrumInter(
    "instrum-inter", cl::desc("Instrument code with inter-procedural parcoach"),
    cl::cat(ParcoachCategory), cl::location(optInstrumInter));
static cl::opt<bool, true> clOptInstrumIntra(
    "instrum-intra", cl::desc("Instrument code with intra-procedural parcoach"),
    cl::cat(ParcoachCategory), cl::location(optInstrumIntra));

static cl::opt<bool, true> clOptWeakUpdate("weak-update",
                                           cl::desc("Weak update"),
                                           cl::cat(ParcoachCategory),
                                           cl::location(optWeakUpdate));
static cl::opt<bool, true>
    clOptNoDataFlow("no-dataflow", cl::desc("Disable dataflow analysis"),
                    cl::cat(ParcoachCategory), cl::location(optNoDataFlow));

static cl::opt<bool, true>
    clOptOmpTaint("check-omp", cl::desc("enable OpenMP collectives checking"),
                  cl::cat(ParcoachCategory), cl::location(optOmpTaint));
static cl::opt<bool, true>
    clOptMpiTaint("check-mpi", cl::desc("enable MPI collectives checking"),
                  cl::cat(ParcoachCategory), cl::location(optMpiTaint));

static cl::opt<bool, true>
    clOptCudaTaint("check-cuda", cl::desc("enable CUDA collectives checking"),
                   cl::cat(ParcoachCategory), cl::location(optCudaTaint));

static cl::opt<bool, true>
    clOptUpcTaint("check-upc", cl::desc("enable UPC collectives checking"),
                  cl::cat(ParcoachCategory), cl::location(optUpcTaint));

static cl::opt<bool, true> clOptVersion("parcoach-version",
                                        cl::desc("Show PARCOACH version"),
                                        cl::cat(ParcoachCategory),
                                        cl::location(optVersion));

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
bool optVersion;
