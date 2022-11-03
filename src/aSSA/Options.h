#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

extern bool optDumpSSA;
extern std::string optDumpSSAFunc;
extern bool optDotGraph;
extern bool optDumpRegions;
extern bool optDumpModRef;
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
extern bool optVersion;

#endif /* OPTIONS_H */
