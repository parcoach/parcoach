#pragma once

#include "llvm/IRReader/IRReader.h"

#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/ValueMap.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"

#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <string>
#include <vector>

using namespace std;
using namespace llvm;

class LocalConcurrencyDetection {

public:
  LocalConcurrencyDetection(){};
  bool runOnModule(Module &M, ModuleAnalysisManager &AM);

private:
  static int count_LOAD, count_STORE, count_inst_STORE, count_inst_LOAD;

  // Instrumentation for dynamic analysis
  bool hasWhiteSucc(BasicBlock *BB);
  void InstrumentMemAccessesRec(Function &F);
  void InstrumentMemAccessesIt(Function &F);
  void DFS_BB(BasicBlock *bb, bool inEpoch);
  bool InstrumentBB(BasicBlock &BB, bool inEpoch);
  bool changeFuncNamesFORTRAN(Instruction &I);
  bool changeFuncNamesC(Instruction &I, CallInst *ci, Function *calledFunction,
                        int line, StringRef file);
  // Debug
  void printBB(BasicBlock *BB);
  // Utils
  void resetCounters();
  enum Color { WHITE, GREY, BLACK };
  using BBColorMap = DenseMap<const BasicBlock *, Color>;
  using MemMap = ValueMap<Value *, Instruction *>;
  BBColorMap ColorMap;
};
