#ifndef UTILS_H
#define UTILS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/PostDominators.h"

#include <vector>

bool isCallSite(const llvm::Instruction *inst);

std::string getValueLabel(const llvm::Value *v);
std::string getCallValueLabel(const llvm::Value *v);

std::vector<llvm::BasicBlock *>
iterated_postdominance_frontier(llvm::PostDominatorTree &PDT,
                                llvm::BasicBlock *BB);

std::set<const llvm::Value *>
computeIPDFPredicates(llvm::PostDominatorTree &PDT, llvm::BasicBlock *BB);

const llvm::Value *getReturnValue(const llvm::Function *F);

double gettime();

bool isIntrinsicDbgFunction(const llvm::Function *F);

bool isIntrinsicDbgInst(const llvm::Instruction *I);

bool functionDoesNotRet(const llvm::Function *F);

const llvm::Value *getBasicBlockCond(const llvm::BasicBlock *BB);

unsigned getBBSetIntersectionSize(const std::set<const llvm::BasicBlock *> S1,
                                  const std::set<const llvm::BasicBlock *> S2);

unsigned
getInstSetIntersectionSize(const std::set<const llvm::Instruction *> S1,
                           const std::set<const llvm::Instruction *> S2);

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
