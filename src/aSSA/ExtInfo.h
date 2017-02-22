#ifndef EXTINFO_H
#define EXTINFO_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Function.h"

#include <map>
#include <vector>

struct extModInfo {
  unsigned nbArgs;
  bool retIsMod;
  std::vector<bool> argIsMod;
};

struct extDepInfo {
  unsigned nbArgs;
  std::map<int, std::vector<int> > argsDeps;
  std::vector<int> retDeps;
};

class ExtInfo {
public:
  ExtInfo(llvm::Module &m);
  ~ExtInfo();

  const extModInfo *getExtModInfo(const llvm::Function *F);
  const extDepInfo *getExtDepInfo(const llvm::Function *F);

private:
  llvm::StringMap<const extModInfo *> extModInfoMap;
  llvm::StringMap<const extDepInfo *> extDepInfoMap;
  llvm::Module &m;
};

#endif /* EXTINFO_H */
