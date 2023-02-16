#ifndef UTILS_H
#define UTILS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/PostDominators.h"

#include <vector>

bool isCallSite(llvm::Instruction const *inst);

std::string getValueLabel(llvm::Value const *v);
std::string getCallValueLabel(llvm::Value const *v);

std::vector<llvm::BasicBlock *>
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
                                llvm::BasicBlock *BB);

std::set<llvm::Value const *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB);

llvm::Value const *getReturnValue(llvm::Function const *F);

bool isIntrinsicDbgFunction(llvm::Function const *F);

bool isIntrinsicDbgInst(llvm::Instruction const *I);

bool functionDoesNotRet(llvm::Function const *F);

llvm::Value const *getBasicBlockCond(llvm::BasicBlock const *BB);

unsigned getBBSetIntersectionSize(const std::set<llvm::BasicBlock const *> S1,
                                  const std::set<llvm::BasicBlock const *> S2);

unsigned
getInstSetIntersectionSize(const std::set<llvm::Instruction const *> S1,
                           const std::set<llvm::Instruction const *> S2);

// This is a small helper to always get a valid - possibly empty - range
// contained in a const container.
// Motivation: operator[] is not const, llvm's lookup is const but obviously
// returns a value and not a reference.
// This attempts to bring the best of both approach: provide a range over the
// contained set/map, using a default empty set/map if it does not exist in
// the map.
template <typename Container, typename KeyTy>
auto getRange(Container const &C, KeyTy K) {
  static_assert(std::is_pointer_v<KeyTy>,
                "getRange must have a KeyTy which is a pointer");
  static decltype(C.lookup(K)) const Empty;
  auto It = C.find(K);
  if (It != C.end()) {
    return llvm::make_range(It->second.begin(), It->second.end());
  } else {
    return llvm::make_range(Empty.begin(), Empty.end());
  }
}

#endif /* UTILS_H */
