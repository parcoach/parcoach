#include "parcoach/RMAPasses.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"

#include <deque>

using namespace llvm;

namespace parcoach::rma {
namespace {
class LCDAnalysis {
  enum Color { WHITE, GREY, BLACK };
  enum ACCESS { READ, WRITE };
  static enum ACCESS getInstructionType(Instruction *I);
  using BBColorMap = DenseMap<BasicBlock const *, Color>;
  using BBMap = DenseMap<BasicBlock const *, bool>;
  using BBMemMap = std::map<BasicBlock *, ValueMap<Value *, Instruction *>>;
  BBColorMap ColorMap;
  BBMap LheaderMap;
  BBMap LlatchMap;
  BBMemMap BBToMemAccess;
  FunctionAnalysisManager &FAM;
  LocalConcurrencyVector Results;

public:
  LCDAnalysis(FunctionAnalysisManager &FAM) : FAM(FAM){};
  LocalConcurrencyVector takeResults() {
    LocalConcurrencyVector Ret;
    std::swap(Results, Ret);
    return Ret;
  }
  void analyze(Function *F);
  void analyzeBB(BasicBlock *B);
  void bfsLoop(Loop *L);
  bool mustWait(BasicBlock *BB);
  bool mustWaitLoop(llvm::BasicBlock *BB, Loop *L);
};

// Get the memory access from the instruction
// We consider only local accesses here
enum LCDAnalysis::ACCESS LCDAnalysis::getInstructionType(Instruction *I) {
  if (CallBase *CI = dyn_cast<CallBase>(I)) {
    if (Function *CalledFunction = CI->getCalledFunction()) {
      StringRef CalledName = CalledFunction->getName();
      if (CalledName == "MPI_Get" || CalledName == "mpi_get_") {
        return WRITE;
      }
      if (CalledName == "MPI_Put" || CalledName == "mpi_put_") {
        return READ;
      }
    }
  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    return WRITE;
  } else if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    return READ;
  }
  // Default return
  return READ;
}

// Store the memory accesses - we keep the memory which is a value and the last
// instruction accessing this memory address
// Later: Check interprocedural information
// Later: keep the accesses per window
void LCDAnalysis::analyzeBB(BasicBlock *BB) {
  Function *F = BB->getParent();
  AAResults &AA = FAM.getResult<AAManager>(*F);

  for (Instruction &Inst : *BB) {
    DebugLoc Dbg = Inst.getDebugLoc(); // get debug infos
    Value *Mem = NULL;
    bool IsLoadOrStore = false;

    // (1) Get memory access
    if (CallBase *CI = dyn_cast<CallBase>(&Inst)) {
      // ci->print(errs());
      // errs() << "\n";
      if (Function *CalledFunction = CI->getCalledFunction()) {
        StringRef CalledName = CalledFunction->getName();
        // errs() << "Calledfunction = " << calledFunction->getName() << "\n";
        if ((CalledName == "MPI_Get") || (CalledName == "MPI_Put") ||
            (CalledName == "mpi_put_") || (CalledName == "mpi_get_")) {
          Mem = CI->getArgOperand(0);
          // errs() << "!!!!!!!! Found a put / get -> store mem \n";
        } else if ((CalledName == "MPI_Win_flush") ||
                   (CalledName == "MPI_Win_flush_all") ||
                   (CalledName == "MPI_Win_fence") ||
                   (CalledName == "MPI_Win_flush_local") ||
                   (CalledName == "MPI_Win_unlock_all") ||
                   (CalledName == "mpi_win_unlock_all_") ||
                   (CalledName == "mpi_win_flush_") ||
                   (CalledName == "mpi_win_flush_all_") ||
                   (CalledName == "mpi_win_fence_")) {
          // GreenErr() << "---> Found a synchro\n";
          BBToMemAccess[BB].clear();
        }
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(&Inst)) {
      Mem = SI->getPointerOperand();
      IsLoadOrStore = true;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(&Inst)) {
      Mem = LI->getPointerOperand();
      IsLoadOrStore = true;
    }

    if (Mem) {
      auto PrevAccess = BBToMemAccess[BB].find(Mem);

      // (2) mem is stored in ValueMap
      if (BBToMemAccess[BB].count(Mem) != 0) {
        if (getInstructionType(PrevAccess->second) == WRITE ||
            getInstructionType(&Inst) == WRITE) {
          // Check if we already report this error
          // if(ConcurrentAccesses[F].count(inst) == 0 &&
          // ConcurrentAccesses[F].count(PrevAccess->second) == 0){
          Results.insert({&Inst, PrevAccess->second});
        } else {
          /*MagentaErr() << "INFO: Memory address already in map - last access
            from instruction: "; PrevAccess->second->print(errs()); errs() <<
            "\n";*/
          if (!IsLoadOrStore) {
            PrevAccess->second = &Inst;
          }
        }
        // (2) mem is not stored in ValueMap - check if a memory stored in
        // ValueMap alias with mem
      } else {
        // errs() << "(2) No memory access found in ValueMap: " <<
        // PrevAccess->second << "\n";
        /*GreenErr() << "DEBUG INFO: no PrevAccess found for instruction: ";
          inst->print(errs());
          errs()  << "\n";*/
        ValueMap<Value *, Instruction *>::iterator It;
        // iterate over the ValueMap to get the first write that alias with mem
        for (It = BBToMemAccess[BB].begin(); It != BBToMemAccess[BB].end();
             It++) {
          if (AA.alias(It->first, Mem) != AliasResult::NoAlias) {
            // if(AA.alias(it->first,mem) == AliasResult::MayAlias)
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found a may alias!\n";
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found an alias!\n";
            if (getInstructionType(It->second) == WRITE ||
                getInstructionType(&Inst) == WRITE) {
              Results.insert({&Inst, It->second});
            }
          }
        }
        // store mem if the instruction is a MPI-RMA
        if (!IsLoadOrStore) {
          BBToMemAccess[BB].insert({Mem, &Inst});
          /*MagentaErr() << "DEBUG INFO: Add new memory access from instruction:
            "; inst->print(errs()); errs() << "\n";*/
        }
      }
    }
  }
}

// If all predecessors have not been set to black, return true otherwise return
// false
bool LCDAnalysis::mustWait(BasicBlock *BB) {
  if (LheaderMap[BB]) {
    // errs() << "is lopp header\n";
    return false; // ignore loop headers
  }
  pred_iterator PI = pred_begin(BB);
  pred_iterator E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (ColorMap[Pred] != BLACK) {
      return true;
    }
  }
  return false;
}

bool LCDAnalysis::mustWaitLoop(llvm::BasicBlock *BB, Loop *L) {
  pred_iterator PI = pred_begin(BB);
  pred_iterator E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    // BB is in the only bb in loop
    if ((Pred == BB) || (LlatchMap[Pred])) {
      continue;
    }
    if (ColorMap[Pred] != BLACK && L->contains(Pred)) {
      // printBB(Pred);
      // errs() << " is white \n";
      return true;
    }
  }
  return false;
}

void LCDAnalysis::bfsLoop(Loop *L) {
  std::deque<BasicBlock *> Unvisited;
  BasicBlock *Lheader = L->getHeader();
  BasicBlock *Llatch = L->getLoopLatch();

  for (Loop *ChildLoop : *L) {
    bfsLoop(ChildLoop);
  }
  /*errs() << ".. BFS on loop containing ..\n";

  for (BasicBlock *BB : L->blocks()) {
          printBB(BB);
  }
  errs() << "....\n";
  */
  Unvisited.push_back(Lheader);
  LheaderMap[L->getHeader()] = true;

  while (!Unvisited.empty()) {
    BasicBlock *Header = *Unvisited.begin();
    // printBB(header);
    Unvisited.pop_front();

    if (ColorMap[Header] == BLACK) {
      continue;
    }
    if (mustWaitLoop(Header, L) &&
        Header != L->getHeader()) { // all predecessors have not been seen
      // errs() << "must wait..\n";
      Unvisited.push_back(Header);
      continue;
    }

    analyzeBB(Header);
    ColorMap[Header] = GREY;

    succ_iterator SI = succ_begin(Header);
    succ_iterator E = succ_end(Header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // Ignore successor not in loop
      if (!(L->contains(Succ))) {
        continue;
      }
      // ignore back edge when the loop has already been checked
      if (LlatchMap[Header] && LheaderMap[Succ]) {
        continue;
      }

      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[Header].begin(),
                                   BBToMemAccess[Header].end());
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[Header].begin(),
                                   BBToMemAccess[Header].end());
      }
    }
    ColorMap[Header] = BLACK;
  }
  // reset BB colors in loop and ignore backedge for the rest of the BFS
  for (BasicBlock *BB : L->blocks()) {
    ColorMap[BB] = WHITE;
  }
  LlatchMap[Llatch] = true;
}

void LCDAnalysis::analyze(Function *F) {
  // All BB must be white at the beginning
  for (BasicBlock &BB : *F) {
    ColorMap[&BB] = WHITE;
    LheaderMap[&BB] = false;
    LlatchMap[&BB] = false;
  }

  std::deque<BasicBlock *> Unvisited;
  BasicBlock &Entry = F->getEntryBlock();
  Unvisited.push_back(&Entry);

  // errs() << ".. BFS on loops ..\n";
  auto &LI = FAM.getResult<LoopAnalysis>(*F);
  for (Loop *L : LI) {
    bfsLoop(L);
  }

  // errs() << ".. BFS ..\n";
  //  BFS
  while (!Unvisited.empty()) {
    BasicBlock *Header = *Unvisited.begin();
    // printBB(header);
    // errs() << "has color = " << ColorMap[header] << "\n";
    Unvisited.pop_front();

    if (ColorMap[Header] == BLACK) {
      continue;
    }

    if (mustWait(Header)) { // all predecessors have not been seen
      // errs() << " must wait \n";
      Unvisited.push_back(Header);
      continue;
    }

    analyzeBB(Header);
    ColorMap[Header] = GREY;

    succ_iterator SI = succ_begin(Header);
    succ_iterator E = succ_end(Header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // ignore back edge
      if (LlatchMap[Header] && LheaderMap[Succ]) {
        continue;
      }
      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[Header].begin(),
                                   BBToMemAccess[Header].end());
        // analyzeBB(Succ, AA);
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[Header].begin(),
                                   BBToMemAccess[Header].end());
      }
    }
    ColorMap[Header] = BLACK;
  }
  // Lheaders.clear();
}
} // namespace

AnalysisKey LocalConcurrencyAnalysis::Key;
LocalConcurrencyVector
LocalConcurrencyAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  TimeTraceScope TTS("LocalConcurrencyAnalysis");
  LCDAnalysis LCD(AM);
  LCD.analyze(&F);

  return LCD.takeResults();
}

} // namespace parcoach::rma
