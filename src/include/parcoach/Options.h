#pragma once

#include "parcoach/Paradigms.h"

#include <string>

namespace parcoach {
struct Options {
  Options();
  static Options const &get();
  // It may be silly as we could just compare in the caller, but this allows
  // for multiple paradigms being enabled in the future.
  // https://llvm.org/docs/CommandLine.html#parsing-a-list-of-options
  bool isActivated(Paradigm P) const;
  Paradigm P;
};

} // namespace parcoach

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
#ifndef NDEBUG
extern bool optDumpModRef;
#endif
