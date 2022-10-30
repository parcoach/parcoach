#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#ifndef NDEBUG
// When LLVM is not built in debug mode, the DebugFlag variable still exists
// and controls whether or not you want debug output; we just mimic Debug.cpp's
// behavior here.
static cl::opt<bool, true> DebugOpt("debug", cl::desc("Enable debug output"),
                                    cl::Hidden, cl::location(DebugFlag));

namespace {
struct DebugOnlyOpt {
  void operator=(const std::string &Val) const {
    if (Val.empty())
      return;
    DebugFlag = true;
    SmallVector<StringRef, 8> dbgTypes;
    StringRef(Val).split(dbgTypes, ',', -1, false);
    std::vector<const char *> rawDbgTypes;
    for (auto dbgType : dbgTypes)
      rawDbgTypes.push_back(dbgType.data());
    setCurrentDebugTypes(rawDbgTypes.data(), rawDbgTypes.size());
  }
};

static cl::opt<DebugOnlyOpt, false, cl::parser<std::string>> DebugOnlyOptVal(
    "debug-only",
    cl::desc("Enable a specific type of debug output (comma separated list "
             "of types)"),
    cl::Hidden, cl::ValueRequired);

} // namespace
#endif
