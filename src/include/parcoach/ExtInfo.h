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
    unsigned NbArgs;
    bool RetIsMod;
    std::vector<bool> ArgIsMod;
  };

  struct DepInfo {
    unsigned NbArgs;
    std::map<int, std::vector<int>> ArgsDeps;
    std::vector<int> RetDeps;
  };

  ExtInfo(llvm::Module &M);
  ~ExtInfo();

  ModInfo const *getExtModInfo(llvm::Function const *F) const;
#if 0
  const DepInfo *getExtDepInfo(const llvm::Function *F) const;
#endif

private:
  llvm::StringMap<ModInfo const *> ExtModInfoMap;
  llvm::StringMap<DepInfo const *> ExtDepInfoMap;
};

class ExtInfoAnalysis : public llvm::AnalysisInfoMixin<ExtInfoAnalysis> {
  friend llvm::AnalysisInfoMixin<ExtInfoAnalysis>;
  static llvm::AnalysisKey Key;

public:
  using Result = std::unique_ptr<ExtInfo>;
  static Result run(llvm::Module &M, llvm::ModuleAnalysisManager &);
};

} // namespace parcoach
