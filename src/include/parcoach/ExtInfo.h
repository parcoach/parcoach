#pragma once

#include "llvm/ADT/StringMap.h"
#include "llvm/Passes/PassBuilder.h"

#include <map>
#include <memory>
#include <vector>

namespace llvm {
class Function;
class Module;
} // namespace llvm

namespace parcoach {
class ExtInfo {
public:
  struct ModInfo {
    unsigned nbArgs;
    bool retIsMod;
    std::vector<bool> argIsMod;
  };

  struct DepInfo {
    unsigned nbArgs;
    std::map<int, std::vector<int>> argsDeps;
    std::vector<int> retDeps;
  };

  ExtInfo(llvm::Module &m);
  ~ExtInfo();

  const ModInfo *getExtModInfo(const llvm::Function *F) const;
#if 0
  const DepInfo *getExtDepInfo(const llvm::Function *F) const;
#endif

private:
  llvm::StringMap<const ModInfo *> extModInfoMap;
  llvm::StringMap<const DepInfo *> extDepInfoMap;
};

class ExtInfoAnalysis : public llvm::AnalysisInfoMixin<ExtInfoAnalysis> {
  friend llvm::AnalysisInfoMixin<ExtInfoAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<ExtInfo>;
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
