#ifndef EXTINFO_H
#define EXTINFO_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Function.h"

#include <vector>

struct extModInfo {
  unsigned nbArgs;
  bool retIsMod;
  std::vector<bool> argIsMod;
};


class ExtInfo {
public:
  ExtInfo();
  ~ExtInfo();

  const extModInfo *getExtModInfo(const llvm::Function *F);

private:
  llvm::StringMap<const extModInfo *> extModInfoMap;
};

#endif /* EXTINFO_H */
