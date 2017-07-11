#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

extern bool optDumpSSA;
extern std::string optDumpSSAFunc;
extern bool optDotGraph;
extern bool optDumpRegions;
extern bool optDumpModRef;
extern bool optTimeStats;
extern bool optDisablePhiElim;
extern bool optDotTaintPaths;
extern bool optStats;
extern bool optNoRegName;
extern bool optContextSensitive;
extern bool optNoInstrum;
extern bool optStrongUpdate;
extern bool optNoPtrDep;
extern bool optNoPred;
extern bool optNoDataFlow;
extern bool optOmpTaint;
extern bool optCudaTaint;
extern bool optMpiTaint;
extern bool optUpcTaint;

void getOptions();

#endif /* OPTIONS_H */
