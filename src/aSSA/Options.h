#pragma once

#include <string>

extern bool optDumpSSA;
extern std::string optDumpSSAFunc;
extern bool optDotGraph;
extern bool optDumpRegions;
extern bool optTimeStats;
extern bool optDotTaintPaths;
extern bool optStats;
extern bool optWithRegName;
extern bool optContextInsensitive;
extern bool optInstrumInter;
extern bool optInstrumIntra;
extern bool optWeakUpdate;
extern bool optNoDataFlow;
extern bool optOmpTaint;
extern bool optCudaTaint;
extern bool optMpiTaint;
extern bool optUpcTaint;
#ifndef NDEBUG
extern bool optDumpModRef;
#endif
