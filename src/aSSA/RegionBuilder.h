#ifndef REGIONBUILDER_H
#define REGIONBUILDER_H

#include "Region.h"
#include "Utils.h"

#include "llvm/IR/InstVisitor.h"

#include <map>
#include <vector>

class RegionBuilder : public llvm::InstVisitor<RegionBuilder> {
public:
  RegionBuilder(std::map<const llvm::Function *,
		std::vector<Region *> > &regMap)
    : regMap(regMap) {}
  ~RegionBuilder() {}

  void visitFunction(llvm::Function &F) {
    if (F.isDeclaration())
      return;

    // Add pointer parameters
    for (auto I = F.arg_begin(), E = F.arg_end(); I != E; ++I) {
      const llvm::Argument *arg = I;
      if (arg->getType()->isPointerTy()) {
	regMap[&F].push_back(new Region(Region::PARAM, arg));
      }
    }
  }

  void visitAllocaInst(llvm::AllocaInst &I) {
    const llvm::Function *F = I.getParent()->getParent();

    // Add stack regions
    regMap[F].push_back(new Region(Region::STACK, &I));
  }

  void visitCallInst(llvm::CallInst &I) {
    const llvm::Function *F = I.getParent()->getParent();

    // Add heap regions
    const llvm::Function *called = I.getCalledFunction();
    if (isMemoryAlloc(called)) {
      regMap[F].push_back(new Region(Region::HEAP, &I));
    }
  }

private:
  std::map<const llvm::Function *, std::vector<Region *> > &regMap;
};

#endif /* REGIONBUILDER_H */
