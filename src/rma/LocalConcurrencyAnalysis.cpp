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
  enum ACCESS getInstructionType(Instruction *I);
  using BBColorMap = DenseMap<BasicBlock const *, Color>;
  using BBMap = DenseMap<BasicBlock const *, bool>;
  using BBMemMap = std::map<BasicBlock *, ValueMap<Value *, Instruction *>>;
  BBColorMap ColorMap;
  BBMap LheaderMap;
  BBMap LlatchMap;
  BBMemMap BBToMemAccess;
  FunctionAnalysisManager &FAM_;
  LocalConcurrencyVector Results;

public:
  LCDAnalysis(FunctionAnalysisManager &FAM) : FAM_(FAM){};
  LocalConcurrencyVector takeResults() {
    LocalConcurrencyVector Ret;
    std::swap(Results, Ret);
    return Ret;
  }
  void analyze(Function *F);
  void analyzeBB(BasicBlock *B);
  void BFS_Loop(Loop *L);
  bool mustWait(BasicBlock *BB);
  bool mustWaitLoop(llvm::BasicBlock *BB, Loop *L);
};

// Get the memory access from the instruction
// We consider only local accesses here
enum LCDAnalysis::ACCESS LCDAnalysis::getInstructionType(Instruction *I) {
  if (CallBase *ci = dyn_cast<CallBase>(I)) {
    if (Function *calledFunction = ci->getCalledFunction()) {
      StringRef CalledName = calledFunction->getName();
      if (CalledName == "MPI_Get" || CalledName == "mpi_get_") {
        return WRITE;
      } else if (CalledName == "MPI_Put" || CalledName == "mpi_put_") {
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
// TODO: Check interprocedural information
// TODO: keep the accesses per window
void LCDAnalysis::analyzeBB(BasicBlock *bb) {
  Function *F = bb->getParent();
  AAResults &AA = FAM_.getResult<AAManager>(*F);

  for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
    Instruction *inst = &*i;
    DebugLoc dbg = inst->getDebugLoc(); // get debug infos
    Value *mem = NULL;
    bool isLoadOrStore = false;

    // (1) Get memory access
    if (CallBase *ci = dyn_cast<CallBase>(inst)) {
      // ci->print(errs());
      // errs() << "\n";
      if (Function *calledFunction = ci->getCalledFunction()) {
        StringRef CalledName = calledFunction->getName();
        // errs() << "Calledfunction = " << calledFunction->getName() << "\n";
        if ((CalledName == "MPI_Get") || (CalledName == "MPI_Put") ||
            (CalledName == "mpi_put_") || (CalledName == "mpi_get_")) {
          mem = ci->getArgOperand(0);
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
          BBToMemAccess[bb].clear();
        }
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(inst)) {
      mem = SI->getPointerOperand();
      isLoadOrStore = true;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(inst)) {
      mem = LI->getPointerOperand();
      isLoadOrStore = true;
    }

    if (mem) {
      auto PrevAccess = BBToMemAccess[bb].find(mem);

      // (2) mem is stored in ValueMap
      if (BBToMemAccess[bb].count(mem) != 0) {
        if (getInstructionType(PrevAccess->second) == WRITE ||
            getInstructionType(inst) == WRITE) {
          // Check if we already report this error
          // if(ConcurrentAccesses[F].count(inst) == 0 &&
          // ConcurrentAccesses[F].count(PrevAccess->second) == 0){
          Results.insert({inst, PrevAccess->second});
        } else {
          /*MagentaErr() << "INFO: Memory address already in map - last access
            from instruction: "; PrevAccess->second->print(errs()); errs() <<
            "\n";*/
          if (!isLoadOrStore)
            PrevAccess->second = inst;
        }
        // (2) mem is not stored in ValueMap - check if a memory stored in
        // ValueMap alias with mem
      } else {
        // errs() << "(2) No memory access found in ValueMap: " <<
        // PrevAccess->second << "\n";
        /*GreenErr() << "DEBUG INFO: no PrevAccess found for instruction: ";
          inst->print(errs());
          errs()  << "\n";*/
        ValueMap<Value *, Instruction *>::iterator it;
        // iterate over the ValueMap to get the first write that alias with mem
        for (it = BBToMemAccess[bb].begin(); it != BBToMemAccess[bb].end();
             it++) {
          if (AA.alias(it->first, mem) != AliasResult::NoAlias) {
            // if(AA.alias(it->first,mem) == AliasResult::MayAlias)
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found a may alias!\n";
            // errs() << "(2) No memory access found in ValueMap: " << it->first
            //<< " but found an alias!\n";
            if (getInstructionType(it->second) == WRITE ||
                getInstructionType(inst) == WRITE) {
              Results.insert({inst, it->second});
            }
          }
        }
        // store mem if the instruction is a MPI-RMA
        if (!isLoadOrStore) {
          BBToMemAccess[bb].insert({mem, inst});
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
  pred_iterator PI = pred_begin(BB), E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (ColorMap[Pred] != BLACK)
      return true;
  }
  return false;
}

bool LCDAnalysis::mustWaitLoop(llvm::BasicBlock *BB, Loop *L) {
  pred_iterator PI = pred_begin(BB), E = pred_end(BB);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    // BB is in the only bb in loop
    if ((Pred == BB) || (LlatchMap[Pred])) {
      continue;
    }
    // TODO: plus complique que ca
    if (ColorMap[Pred] != BLACK && L->contains(Pred)) {
      // printBB(Pred);
      // errs() << " is white \n";
      return true;
    }
  }
  return false;
}

void LCDAnalysis::BFS_Loop(Loop *L) {
  std::deque<BasicBlock *> Unvisited;
  BasicBlock *Lheader = L->getHeader();
  BasicBlock *Llatch = L->getLoopLatch();

  for (Loop *ChildLoop : *L) {
    BFS_Loop(ChildLoop);
  }
  /*errs() << ".. BFS on loop containing ..\n";

  for (BasicBlock *BB : L->blocks()) {
          printBB(BB);
  }
  errs() << "....\n";
  */
  Unvisited.push_back(Lheader);
  LheaderMap[L->getHeader()] = true;

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // printBB(header);
    Unvisited.pop_front();

    if (ColorMap[header] == BLACK)
      continue;
    if (mustWaitLoop(header, L) &&
        header != L->getHeader()) { // all predecessors have not been seen
      // errs() << "must wait..\n";
      Unvisited.push_back(header);
      continue;
    }

    analyzeBB(header);
    ColorMap[header] = GREY;

    succ_iterator SI = succ_begin(header), E = succ_end(header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // Ignore successor not in loop
      if (!(L->contains(Succ)))
        continue;
      // ignore back edge when the loop has already been checked
      if (LlatchMap[header] && LheaderMap[Succ]) {
        continue;
      }

      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // TODO: For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
      }
    }
    ColorMap[header] = BLACK;
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
  BasicBlock &entry = F->getEntryBlock();
  Unvisited.push_back(&entry);

  // errs() << ".. BFS on loops ..\n";
  auto &LI = FAM_.getResult<LoopAnalysis>(*F);
  for (Loop *L : LI) {
    BFS_Loop(L);
  }

  // errs() << ".. BFS ..\n";
  //  BFS
  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // printBB(header);
    // errs() << "has color = " << ColorMap[header] << "\n";
    Unvisited.pop_front();

    if (ColorMap[header] == BLACK)
      continue;

    if (mustWait(header)) { // all predecessors have not been seen
      // errs() << " must wait \n";
      Unvisited.push_back(header);
      continue;
    }

    analyzeBB(header);
    ColorMap[header] = GREY;

    succ_iterator SI = succ_begin(header), E = succ_end(header);
    for (; SI != E; ++SI) {
      BasicBlock *Succ = *SI;
      // ignore back edge
      if (LlatchMap[header] && LheaderMap[Succ])
        continue;
      // Succ not seen before
      if (ColorMap[Succ] == WHITE) {
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
        // analyzeBB(Succ, AA);
        ColorMap[Succ] = GREY;
        Unvisited.push_back(Succ);
        // Succ already seen
      } else {
        // merge the memory accesses from the previous paths - only local errors
        // detection
        // TODO: For latter: report concurrent one-sided
        BBToMemAccess[Succ].insert(BBToMemAccess[header].begin(),
                                   BBToMemAccess[header].end());
      }
    }
    ColorMap[header] = BLACK;
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
