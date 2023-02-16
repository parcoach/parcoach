#include "ParcoachAnalysisInter.h"
#include "Instrumentation.h"
#include "Utils.h"
#include "parcoach/Collectives.h"
#include "parcoach/DepGraphDCF.h"
#include "parcoach/Options.h"

#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/SourceMgr.h"

#include <deque>
#include <utility>
#include <vector>

#define DEBUG_TYPE "bfs"

using namespace llvm;

namespace parcoach {
int ParcoachAnalysisInter::id = 0;

int nbColNI = 0;

namespace {
template <typename T>
void push(std::unique_ptr<CollList> &List, T Collective, BasicBlock const *BB,
          bool Force = false) {
  if (List) {
    List->push(Collective, BB, Force);
  } else {
    List = std::make_unique<CollList>(Collective, BB);
  }
}

cl::opt<bool> optNoDataFlow("no-dataflow",
                            cl::desc("Disable dataflow analysis"),
                            cl::cat(ParcoachCategory));

} // namespace

Warning::Warning(Function const *F, DebugLoc &DL, ConditionalsContainerTy &&C)
    : Missed(F), Where(DL), Conditionals(C) {
  // Make sure lines are displayed in order.
  llvm::sort(Conditionals, [](DebugLoc const &A, DebugLoc const &B) {
    return (A ? A.getLine() : 0) < (B ? B.getLine() : 0);
  });
}

std::string Warning::toString() const {
  assert(Missed != nullptr && "toString called on an invalid Warning");

  std::string Res;
  raw_string_ostream OS(Res);
  auto Line = Where ? Where.getLine() : 0;
  OS << Missed->getName() << " line " << Line;
  OS << " possibly not called by all processes because of conditional(s) "
        "line(s) ";

  for (auto &Loc : Conditionals) {
    OS << " " << (Loc ? std::to_string(Loc.getLine()) : "?");
    OS << " (" << (Loc ? Loc->getFilename() : "?") << ")";
  }
  OS << " (full-inter)";
  return Res;
}

void ParcoachAnalysisInter::run() {
  TimeTraceScope TTS("ParcoachAnalysisInter");
  // Parcoach analysis

  /* (1) BFS on each function of the Callgraph in reverse topological order
   *  -> set a function summary with sequence of collectives
   *  -> keep a set of collectives per BB and set the conditionals at NAVS if
   *     it can lead to a deadlock
   */
  LLVM_DEBUG(dbgs() << " (1) BFS\n");
  scc_iterator<PTACallGraph const *> cgSccIter = scc_begin(&PTACG);
  while (!cgSccIter.isAtEnd()) {
    auto const &nodeVec = *cgSccIter;
    for (PTACallGraphNode const *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(*F))
        continue;
      // DBG: errs() << "Function: " << F->getName() << "\n";

      if (Options::get().isActivated(Paradigm::MPI))
        MPI_BFS(F);
      else
        BFS(F);
    } // END FOR
    ++cgSccIter;
  }

  /* (2) Check collectives */
  LLVM_DEBUG(dbgs() << " (2) CheckCollectives\n");
  cgSccIter = scc_begin(&PTACG);
  while (!cgSccIter.isAtEnd()) {
    auto const &nodeVec = *cgSccIter;
    for (PTACallGraphNode const *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(*F))
        continue;
      // DBG: //errs() << "Function: " << F->getName() << "\n";
      checkCollectives(F);
    }
    ++cgSccIter;
  }
  LLVM_DEBUG(dbgs() << " ... Parcoach analysis done\n");
}

bool ParcoachAnalysisInter::useDataflow() const { return !optNoDataFlow; }

/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
void ParcoachAnalysisInter::setCollSet(BasicBlock *BB) {
  LLVM_DEBUG(dbgs() << "setCollSet " << BB << "\n");
  for (auto i = BB->rbegin(), e = BB->rend(); i != e; ++i) {
    Instruction const *inst = &*i;

    // errs() << "call? " << dyn_cast<CallInst>(inst) << "\n";

    if (CallInst const *CI = dyn_cast<CallInst>(inst)) {
      Function *callee = CI->getCalledFunction();

      // if (callee != NULL)
      //   errs() << "collective? " << callee->getName() << "\n";

      //// Indirect calls
      if (callee == NULL) {
        for (Function const *mayCallee :
             PTACG.getIndirectCallMap().lookup(inst)) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          // Is it a function containing collectives?
          if (!collperFuncMap[mayCallee].empty()) // && collMap[BB]!="NAVS")
            collMap[BB] = collperFuncMap[mayCallee] + " " + collMap[BB];
          // Is it a collective operation?
          if (Collective::isCollective(*mayCallee)) {
            std::string OP_name = mayCallee->getName().str();
            if (collMap[BB].empty())
              collMap[BB] = OP_name;
            else
              collMap[BB] = OP_name + " " + collMap[BB];
          }
        }
        //// Direct calls
      } else {
        // Is it a function containing collectives?
        if (!collperFuncMap[callee].empty()) {
          if (collperFuncMap[callee] == "NAVS" && collMap[BB] == "NAVS") {
            collMap[BB] = "NAVS";
          } else {
            collMap[BB] = collperFuncMap[callee] + " " + collMap[BB];
          }
        }
        // Is it a collective operation?
        if (Collective::isCollective(*callee)) {
          std::string OP_name = callee->getName().str();
          if (collMap[BB].empty())
            collMap[BB] = OP_name;
          else
            collMap[BB] = OP_name + " " + collMap[BB];
        }
      }
    }
  }
  LLVM_DEBUG(dbgs() << "Result: " << collMap[BB] << "\n");
}

#ifndef NDEBUG
void ParcoachAnalysisInter::dump() {
  errs() << "=== Dumping MPI map\n";
  for (auto &[BB, Map] : mpiCollListMap) {
    errs() << " - BB " << BB << "\n";
    for (auto &[Val, List] : Map) {
      errs() << "   - Val " << Val << ", {"
             << (List ? List->toString() : "nullptr") << "}"
             << "\n";
    }
    errs() << "\n--------\n";
  }
  errs() << "=== Dumping map\n";
  for (auto &[BB, Coll] : collMap) {
    errs() << " - BB " << BB << ", " << BB->getName() << ": " << Coll << "\n";
    BB->print(errs());
    errs() << "\n--------\n";
  }
  errs() << "=== done\n";
}
#endif

// Get the sequence of collectives in a BB, per MPI communicator
void ParcoachAnalysisInter::setMPICollSet(BasicBlock *BB) {
  LLVM_DEBUG(dbgs() << "== setMPICollSet on " << BB << "\n");
  for (auto i = BB->rbegin(), e = BB->rend(); i != e; ++i) {
    Instruction const *inst = &*i;
    if (CallInst const *CI = dyn_cast<CallInst>(inst)) {
      Function *callee = CI->getCalledFunction();

      //// Indirect call
      if (callee == NULL) {
        LLVM_DEBUG(dbgs() << "Indirect call map size: "
                          << PTACG.getIndirectCallMap().lookup(inst).size()
                          << "\n");
        for (Function const *mayCallee :
             PTACG.getIndirectCallMap().lookup(inst)) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          auto const &CollListForCallee =
              mpiCollListMap[&mayCallee->getEntryBlock()];
          LLVM_DEBUG(dbgs() << "Callee: " << mayCallee->getName()
                            << ", contains collective: "
                            << CollListForCallee.size() << "\n");
          // Is it a function containing collectives?
          if (!CollListForCallee.empty()) { // && mpiCollMap[BB]!="NAVS"){
            // auto &BBcollMap = mpiCollMap[BB];

            for (auto &[Val, List] : CollListForCallee) {
              push(mpiCollListMap[BB][Val], List.get(), BB);
            }
          }
          // Is it a collective operation?
          Collective const *Coll = Collective::find(*mayCallee);
          if (auto *MPIColl = dyn_cast_or_null<MPICollective>(Coll)) {
            StringRef OP_name = mayCallee->getName();
            int OP_arg_id = MPIColl->CommArgId;
            Value *OP_com = nullptr;

            if (OP_arg_id >= 0) {
              OP_com = CI->getArgOperand(OP_arg_id);
              push(mpiCollListMap[BB][OP_com], OP_name, BB);
            } else {
              // Case of collective are effect on all com (ex: MPI_Finalize)
              for (auto &[Val, List] : mpiCollListMap[BB]) {
                push(List, OP_name, BB);
              }
            }
          }
        }
        //// Direct calls
      } else {
        auto const &CollListForF = mpiCollListMap[&callee->getEntryBlock()];
        // Is it a function containing collectives?
        for (auto &[Val, List] : CollListForF) {
          push(mpiCollListMap[BB][Val], List.get(), BB);
        }
        // Is it a collective operation?
        Collective const *Coll = Collective::find(*callee);
        if (auto *MPIColl = dyn_cast_or_null<MPICollective>(Coll)) {
          StringRef OP_name = callee->getName();
          int OP_arg_id = MPIColl->CommArgId;
          Value *OP_com = nullptr;

          if (OP_arg_id >= 0) {
            OP_com = CI->getArgOperand(OP_arg_id);
            push(mpiCollListMap[BB][OP_com], OP_name, BB);
          } else {
            // Case of collective are effect on all com (ex: MPI_Finalize)
            for (auto &[Val, List] : mpiCollListMap[BB]) {
              push(List, OP_name, BB);
            }
          }
        }
      }
    }
  }
  LLVM_DEBUG(dbgs() << "== end setMPICollSet\n");
}

// Return true if the basic block is an exit node
bool ParcoachAnalysisInter::isExitNode(llvm::BasicBlock *BB) {
  if (isa<ReturnInst>(BB->getTerminator()))
    return true;

  // This nodes as no successors
  if (succ_begin(BB) == succ_end(BB)) {
    return true;
  }

  for (auto &I : *BB) {
    Instruction *i = &I;

    CallInst *CI = dyn_cast<CallInst>(i);
    if (!CI)
      continue;

    Function *f = CI->getCalledFunction();
    if (!f)
      continue;

    if ( // f->getName().equals("MPI_Finalize") ||
        f->getName().equals("MPI_Abort") || f->getName().equals("abort") ||
        f->getName().equals("exit") || f->getName().equals("__assert_fail")) {
      return true;
    }
  }
  return false;
}

void ParcoachAnalysisInter::cmpAndUpdateMPICollSet(llvm::BasicBlock *header,
                                                   llvm::BasicBlock *pred) {
  LLVM_DEBUG(dbgs() << "----- cmpAndUpdate -----\n");
  LLVM_DEBUG(dbgs() << "Pred: " << pred << ", Header: " << header << "\n");
  SetVector<Value const *> comm;

  for (auto &com : mpiCollListMap[pred])
    comm.insert(com.first);

  for (auto &com : mpiCollListMap[header])
    comm.insert(com.first);

  for (auto &com : comm) {
    auto &pred_cl = mpiCollListMap[pred][com];
    auto &header_cl = mpiCollListMap[header][com];

    // Header and old_header are nullptr
    if (!pred_cl && !header_cl) {
      continue;
    }

    // Update old_header if pred is the source of the list

    LLVM_DEBUG({
      dbgs() << " ** (oh == h) = "
             << ((pred_cl && !pred_cl->isSource(pred) && header_cl &&
                  (*pred_cl == *header_cl))
                     ? "true"
                     : "false")
             << "\n"
             << "     p = " << (pred_cl ? pred_cl->toCollMap() : "") << "\n"
             << " IsSrc = " << (pred_cl ? pred_cl->isSource(pred) : false)
             << "\n"
             << "     h = " << (header_cl ? header_cl->toCollMap() : "") << "\n"
             << "   fun = "
             << (header ? header->getParent()->getName().str()
                        : (pred ? pred->getParent()->getName().str() : "xxx"))
             << "\n";
    });

    // Equals
    // FIXME: maybe compare sources in operator<?
    if (pred_cl && !pred_cl->isSource(pred) && header_cl &&
        (*pred_cl == *header_cl)) {
      LLVM_DEBUG(dbgs() << "----- end cmpAndUpdate -----\n");
      return;
    }

    // Not equals
    if (pred_cl && !pred_cl->isNAVS()) {
      pred_cl->push("NAVS", pred);
    }
  }
  LLVM_DEBUG(dbgs() << "----- end cmpAndUpdate -----\n");
}

void ParcoachAnalysisInter::MPI_BFS_Loop(llvm::Loop *L) {
  LLVM_DEBUG(dbgs() << "----- MPI_BFS_Loop -----\n");
  std::deque<BasicBlock *> Unvisited;
  BasicBlock *Lheader = L->getHeader();

  // COMPUTE SUB-LOOPS (add the header to unvisited nodes)
  for (auto &sl : *L) {
    Unvisited.push_back(sl->getHeader());
    MPI_BFS_Loop(sl);
  }

  // GET ALL EXIT NODES DEPENDING OF THIS LOOP
  llvm::ArrayRef<BasicBlock *> BB = L->getBlocks();
  for (int i = 0; i < BB.size(); i++) {
    if (!L->contains(BB[i]))
      continue;

    // Skip loop headers successors exit node
    if (BB[i] == Lheader)
      continue;

    // Is exit node
    if (isExitNode(BB[i]) && bbVisitedMap[BB[i]] != black) {
      Unvisited.push_back(BB[i]);
      setMPICollSet(BB[i]);
      bbVisitedMap[BB[i]] = grey;
    }

    // As an exit node as successor
    succ_iterator PI = succ_begin(BB[i]), E = succ_end(BB[i]);
    for (; PI != E; ++PI) {
      BasicBlock *Succ = *PI;

      // Exit nodes not yet visited
      if (isExitNode(Succ) && !L->contains(Succ) &&
          bbVisitedMap[Succ] != black) {
        Unvisited.push_back(Succ);
        setMPICollSet(Succ);
        bbVisitedMap[Succ] = grey;
      }
    }
  }

  Unvisited.push_back(Lheader);

  while (Unvisited.size() > 0) {
    LLVM_DEBUG(dbgs() << "Unvisited (" << Unvisited.size() << ")\n");
    BasicBlock *header = *Unvisited.begin();
    // DEBUG//errs() << "Header " << header->getName() << "\n";
    Unvisited.pop_front();

    if (bbVisitedMap[header] == black)
      continue;

    if (mustWait(header, L) &&
        header != Lheader) { // all successors have not been seen
      Unvisited.push_back(header);
      continue;
    }

    pred_iterator PI = pred_begin(header), E = pred_end(header);
    for (; PI != E; ++PI) {
      BasicBlock *Pred = *PI;
      if (!L->contains(Pred)) // ignore BB not in the loop
        continue;

      // BB NOT SEEN BEFORE
      if (bbVisitedMap[Pred] == white) {
        for (auto &[Val, List] : mpiCollListMap[header]) {
          if (List) {
            mpiCollListMap[Pred][Val] = std::make_unique<CollList>(*List);
          }
        }

        setMPICollSet(Pred);
        bbVisitedMap[Pred] = grey;

        if (Pred != Lheader && (L->contains(Pred) || isExitNode(Pred))) {
          Unvisited.push_back(Pred);
        }
      }
      // BB ALREADY SEEN
      else {
        cmpAndUpdateMPICollSet(header, Pred);
      } // END ELSE
    }   // END FOR
    bbVisitedMap[header] = black;
  } // END WHILE

  // Reset Lheader color
  bbVisitedMap[Lheader] = grey;

  // Loop with collective are alaways NAVS
  for (auto &[Val, List] : mpiCollListMap[Lheader]) {
    push(List, "NAVS", Lheader, true);
  }

  LLVM_DEBUG(dbgs() << "----- end MPI_BFS_Loop -----\n");
}

// FOR MPI APPLIS: BFS
void ParcoachAnalysisInter::MPI_BFS(llvm::Function *F) {
  LLVM_DEBUG(dbgs() << "-- MPI_BFS --\n");
  std::deque<BasicBlock *> Unvisited;

  // DEBUG//errs() << "** Analyzing function " << F->getName() << "\n";

  // GET ALL EXIT NODES
  for (BasicBlock &I : *F) {
    // Set all nodes to white
    bbVisitedMap[&I] = white;
    // Return inst / exit nodes
    if (isExitNode(&I)) {
      Unvisited.push_back(&I);
      // errs() << "Exit node: " << I.getName() << "\n";
      setMPICollSet(&I);
      // bbVisitedMap[&I] = grey;
    }
  }

  // BFS ON EACH LOOP IN F
  // DEBUG//errs() << "-- BFS IN EACH LOOP --\n";
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  LoopInfo &LI = FAM.getResult<LoopAnalysis>(*const_cast<Function *>(F));
  for (Loop *L : LI) {
    Unvisited.push_back(L->getHeader());
    MPI_BFS_Loop(L);
  }

  // DEBUG//errs() << "-- BFS --\n";
  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    Unvisited.pop_front();

    if (bbVisitedMap[header] == black)
      continue;

    // DEBUG//errs() << "Header " << header->getName() << "\n";
    if (mustWait(header)) { // all successors have not been seen
      Unvisited.push_back(header);
      continue;
    }

    pred_iterator PI = pred_begin(header), E = pred_end(header);
    for (; PI != E; ++PI) {
      BasicBlock *Pred = *PI;

      // BB NOT SEEN BEFORE
      if (bbVisitedMap[Pred] == white) {
        for (auto &[Val, List] : mpiCollListMap[header]) {
          if (List) {
            mpiCollListMap[Pred][Val] = std::make_unique<CollList>(*List);
          }
        }

        setMPICollSet(Pred);

        bbVisitedMap[Pred] = grey;
        Unvisited.push_back(Pred);
        // BB ALREADY SEEN
      } else {
        cmpAndUpdateMPICollSet(header, Pred);
      } // END ELSE
    }   // END FOR

    bbVisitedMap[header] = black;
  } // END WHILE
  LLVM_DEBUG(dbgs() << "-- end MPI_BFS --\n");
}

// FOR BFS: Returns true if all successors are not black
bool ParcoachAnalysisInter::mustWait(llvm::BasicBlock *bb, Loop *l) {
  for (auto *S : successors(bb)) {
    if (S == bb) {
      continue;
    }
    if (bbVisitedMap[S] != black && (!l || l->contains(S))) {
      return true;
    }
  }
  return false;
}

// BFS ON EACH LOOP
void ParcoachAnalysisInter::BFS_Loop(llvm::Loop *L) {
  std::deque<BasicBlock *> Unvisited;
  BasicBlock *Lheader = L->getHeader();

  // COMPUTE SUB-LOOPS (add the header to unvisited nodes)
  for (auto &sl : *L) {
    Unvisited.push_back(sl->getHeader());
    BFS_Loop(sl);
  }

  // GET ALL EXIT NODES DEPENDING OF THIS LOOP
  llvm::ArrayRef<BasicBlock *> BB = L->getBlocks();
  for (int i = 0; i < BB.size(); i++) {
    if (!L->contains(BB[i]))
      continue;

    // Skip loop headers successors exit node
    if (BB[i] == Lheader)
      continue;

    // Is exit node
    if (isExitNode(BB[i]) && bbVisitedMap[BB[i]] != black) {
      Unvisited.push_back(BB[i]);
      setCollSet(BB[i]);
      bbVisitedMap[BB[i]] = grey;
    }

    // As an exit node as successor
    succ_iterator PI = succ_begin(BB[i]), E = succ_end(BB[i]);
    for (; PI != E; ++PI) {
      BasicBlock *Succ = *PI;

      // Exit nodes not yet visited
      if (isExitNode(Succ) && !L->contains(Succ) &&
          bbVisitedMap[Succ] != black) {
        Unvisited.push_back(Succ);
        setCollSet(Succ);
        bbVisitedMap[Succ] = grey;
      }
    }
  }

  // BFS
  Unvisited.push_back(Lheader); // Starting node

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // DEBUG//errs() << "Header " << header->getName() << "\n";
    Unvisited.pop_front();

    if (bbVisitedMap[header] == black)
      continue;

    if (mustWait(header, L) &&
        header != Lheader) { // all successors have not been seen
      Unvisited.push_back(header);
      continue;
    }

    pred_iterator PI = pred_begin(header), E = pred_end(header);
    for (; PI != E; ++PI) {
      BasicBlock *Pred = *PI;

      if (!L->contains(Pred)) // ignore BB not in the loop
        continue;

      // BB NOT SEEN BEFORE
      if (bbVisitedMap[Pred] == white) {
        // DEBUG//errs() << F->getName() << " Pred " << Pred->getName() <<
        // "\n";
        collMap[Pred] = collMap[header];
        setCollSet(Pred);
        // DEBUG//errs() << "  -> has " << collMap[Pred] << "\n";
        bbVisitedMap[Pred] = grey;
        if (Pred != Lheader)
          Unvisited.push_back(Pred);
      }
      // BB ALREADY SEEN
      else {
        // DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << "
        // already seen\n";
        std::string temp = collMap[Pred];
        collMap[Pred] = collMap[header];
        setCollSet(Pred);
        // DEBUG//errs() << collMap[Pred] << " = " << temp << "?\n";
        if (temp != collMap[Pred]) {
          collMap[Pred] = "NAVS";
          // DEBUG//errs() << "  -> BB has " << collMap[Pred] << "\n";
        }
        // XXX: Loop always set NAVS to avoid different loop iterations
        collMap[Pred] = "NAVS";
      }

      bbVisitedMap[header] = black;

    } // END FOR
  }   // END WHILE

  // Reset Lheader color
  bbVisitedMap[Lheader] = grey;
}

// BFS
void ParcoachAnalysisInter::BFS(llvm::Function *F) {
  std::deque<BasicBlock *> Unvisited;

  LLVM_DEBUG(dbgs() << "-- BFS --\n");
  // GET ALL EXIT NODES
  for (BasicBlock &I : *F) {
    // Set all nodes to white
    bbVisitedMap[&I] = white;

    // Exit node
    if (isExitNode(&I)) {
      Unvisited.push_back(&I);
      setCollSet(&I);
      bbVisitedMap[&I] = grey;
    }
  }

  // BFS ON EACH LOOP IN F
  LLVM_DEBUG(dbgs() << "-- BFS IN EACH LOOP --\n");
  auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  LoopInfo &LI = FAM.getResult<LoopAnalysis>(*const_cast<Function *>(F));
  for (Loop *L : LI) {
    Unvisited.push_back(L->getHeader());
    BFS_Loop(L);
  }

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    Unvisited.pop_front();

    // Avoid duplication
    if (bbVisitedMap[header] == black)
      continue;

    if (mustWait(header)) { // all successors have not been seen
      Unvisited.push_back(header);
      continue;
    }

    pred_iterator PI = pred_begin(header), E = pred_end(header);
    for (; PI != E; ++PI) {
      BasicBlock *Pred = *PI;

      // BB NOT SEEN BEFORE
      if (bbVisitedMap[Pred] == white) {
        // DEBUG//errs() << F->getName() << " Pred " << Pred->getName() << "\n";
        // Loop header may have a collMap
        if (collMap[Pred].empty())
          collMap[Pred] = collMap[header];
        else // if not empty, it should be a loop header with NAVS
          collMap[Pred] = "NAVS";

        setCollSet(Pred);
        bbVisitedMap[Pred] = grey;
        if (header != Pred) // to handle loops of size 1
          Unvisited.push_back(Pred);
      }
      // BB ALREADY SEEN
      else {
        // DEBUG//errs() << F->getName() << " - BB " << Pred->getName() << "
        // already seen\n";
        std::string temp = collMap[Pred];
        collMap[Pred] = collMap[header];
        setCollSet(Pred);
        // DEBUG//errs() << collMap[Pred] << " = " << temp << "?\n";
        if (temp != collMap[Pred])
          collMap[Pred] = "NAVS";
        // DEBUG//errs() << "  -> BB has " << collMap[Pred] << "\n";
      }
      bbVisitedMap[header] = black;
    }
  } // END WHILE

  BasicBlock &entry = F->getEntryBlock();
  collperFuncMap[F] = collMap[&entry];
  // if(F->getName() == "main")
  // DEBUG//errs() << F->getName() << " summary = " << collperFuncMap[F] <<
  // "\n";
  LLVM_DEBUG(dbgs() << "-- END BFS --\n");
}

// COUNT COLLECTIVES TO INST AND INSTRUMENT THEM
// Not correct..
#if 0
void ParcoachAnalysisInter::countCollectivesToInst(llvm::Function *F) {
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    bool toinstrument = false;
    Instruction *i = &*I;
    CallInst *CI = dyn_cast<CallInst>(i);
    if (!CI)
      continue;
    Function *f = CI->getCalledFunction();
    if (!f)
      continue;
    std::string OP_name = f->getName().str();
    std::string Warning = getWarning(*i);
    DebugLoc locC = i->getDebugLoc();
    int OP_line = 0;
    std::string File = "";
    if (locC) {
      OP_line = locC.getLine();
      File = locC->getFilename().str();
    }
    // Is it a collective call?
    if (!isCollective(f)) {
      // check if MPI_Finalize or abort for instrumentation
      if (f->getName().equals("MPI_Finalize") ||
          f->getName().equals("MPI_Abort") || f->getName().equals("abort")) {
        LLVM_DEBUG(dbgs() << "-> insert a check before " << OP_name << " line "
                          << OP_line << "\n");
        insertCC(i, v_coll.size() + 1, OP_name, OP_line, Warning, File);
        // nbCC++;
      }
      continue;
    }
    // Get collective infos
    int OP_color = getCollectiveColor(f);
    Value *OP_com = CI->getArgOperand(Com_arg_id(OP_color));
    // Get conditionals from the callsite
    std::set<const BasicBlock *> callIPDF;
    DG->getCallInterIPDF(CI, callIPDF);

    for (const BasicBlock *BB : callIPDF) {
      // Is this node detected as potentially dangerous by parcoach?
      if (!optMpiTaint && collMap[BB] != "NAVS")
        continue;
      if (optMpiTaint && mpiCollListMap[BB][OP_com] &&
          !mpiCollListMap[BB][OP_com]->isNAVS())
        continue;

      // Is this condition tainted?
      const Value *cond = getBasicBlockCond(BB);
      if (!cond || (!optNoDataFlow && !DG->isTaintedValue(cond))) {
        const Instruction *instE = BB->getTerminator();
        DebugLoc locE = instE->getDebugLoc();
        continue;
      }
      toinstrument = true;
    } // END FOR

    if (toinstrument) {
      nbColNI++; // collective to instrument
      // Instrument
      LLVM_DEBUG(dbgs() << "-> insert a check before " << OP_name << " line "
                        << OP_line << "\n");
      insertCountColl(i, OP_name, OP_line, File, 1);
      insertCC(i, OP_color, OP_name, OP_line, Warning, File);
      nbCC++;
      // If the coll is in a function f, all conds not in f has NAVS to
      // instrument them
      for (const BasicBlock *BB : callIPDF) {
        // if BB not in the same function, set it as NAVS
        const llvm::Function *fBB = BB->getParent();
        if (F != fBB)
          mpiCollListMap[BB][OP_com] =
              new CollList("NAVS", mpiCollListMap[BB][OP_com], BB);
        // mpiCollMap[BB][OP_com] = "NAVS";
      }
    } else {
      // insert count_collectives(const char* OP_name, int OP_line, char
      // *FILE_name,int inst)
      insertCountColl(i, OP_name, OP_line, File, 0);
      // insertCC(i,OP_color, OP_name, OP_line, Warning, File);
    }

  } // END FOR
}
#endif

// CHECK COLLECTIVES FUNCTION
void ParcoachAnalysisInter::checkCollectives(Function *F) {
  LLVM_DEBUG(dbgs() << "Running on " << F->getName() << "\n");
  LLVM_DEBUG(dump());
  auto IsaDirectCallToCollective = [](Instruction const &I) {
    if (CallInst const *CI = dyn_cast<CallInst>(&I)) {
      Function const *F = CI->getCalledFunction();
      return F && Collective::isCollective(*F);
    }
    return false;
  };
  auto Candidates =
      make_filter_range(instructions(F), IsaDirectCallToCollective);
  for (Instruction &I : Candidates) {
    // Debug info (line in the source code, file)
    // Warning info
    char const *ProgName = "PARCOACH";
    SMDiagnostic Diag;
    SmallVector<DebugLoc, 2> Conditionals;

    CallInst &CI = cast<CallInst>(I);
    Function &F = *CI.getCalledFunction();

    LLVM_DEBUG({
      dbgs() << "Running on ";
      CI.print(dbgs());
      dbgs() << "\n";
    });

    Collective const *Coll = Collective::find(F);
    assert(Coll && "Coll expected to be not null because of filter");
    Value *OP_com = nullptr;
    int OP_arg_id = -1;

    // If we are here the paradigm must be enabled because isCollective/find
    // checks that the collective's paradigm is enabled.
    if (auto *MPIColl = dyn_cast<MPICollective>(Coll)) {
      OP_arg_id = MPIColl->CommArgId;
      if (OP_arg_id >= 0)
        OP_com = CI.getArgOperand(OP_arg_id); // 0 for Barrier only
    }

    // CI->getArgOperand(0)->dump();
    // errs() << "Found " << OP_name << " on " << OP_com << " line " << OP_line
    // << " in " << File << "\n";

    nbCollectivesFound++;
    bool isColWarning = false;
    bool isColWarningParcoach = false;

    // Get conditionals from the callsite
    std::set<BasicBlock const *> callIPDF;
    DG->getCallInterIPDF(&CI, callIPDF);
    // For the summary-based approach, use the following instead of the previous
    // line
    // DG->getCallIntraIPDF(CI, callIPDF);
    LLVM_DEBUG(dbgs() << "callIPDF size: " << callIPDF.size() << "\n");
    if (!callIPDF.empty())
      nbCollectivesCondCalled++;

    for (BasicBlock const *BB : callIPDF) {
      // Is this node detected as potentially dangerous by parcoach?
      if (!Options::get().isActivated(Paradigm::MPI) && collMap[BB] != "NAVS") {
        continue;
      }
      if (Options::get().isActivated(Paradigm::MPI) && OP_arg_id >= 0 &&
          mpiCollListMap[BB][OP_com] && !mpiCollListMap[BB][OP_com]->isNAVS()) {
        continue;
      }

      isColWarningParcoach = true;
      conditionSetParcoachOnly.insert(BB);

      // Is this condition tainted?
      Value const *cond = getBasicBlockCond(BB);

      if (!cond || (!optNoDataFlow && !DG->isTaintedValue(cond))) {
        Instruction const *instE = BB->getTerminator();
        DebugLoc locE = instE->getDebugLoc();
        // errs() << "-> not tainted\n";
        continue;
      }

      isColWarning = true;
      conditionSet.insert(BB);

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      Instruction const *inst = BB->getTerminator();
      DebugLoc loc = inst->getDebugLoc();
      Conditionals.push_back(loc);

      if (EmitDotDG_) {
        std::string dotfilename("taintedpath-");
        std::string cfilename = loc->getFilename().str();
        size_t lastpos_slash = cfilename.find_last_of('/');
        if (lastpos_slash != cfilename.npos)
          cfilename = cfilename.substr(lastpos_slash + 1, cfilename.size());
        dotfilename.append(cfilename).append("-");
        dotfilename.append(std::to_string(loc.getLine())).append(".dot");
        DG->dotTaintPath(cond, dotfilename, &CI);
      }
    } // END FOR

    // Is there at least one node from the IPDF+ detected as potentially
    // dangerous by parcoach
    if (isColWarningParcoach) {
      warningSetParcoachOnly.insert(&CI);
    }

    // Is there at least one node from the IPDF+ tainted
    if (!isColWarning)
      continue;

    warningSet.insert(&CI);
    DebugLoc DLoc = CI.getDebugLoc();
    auto [Entry, _] =
        Warnings.insert({&CI, Warning(&F, DLoc, std::move(Conditionals))});

    Diag = SMDiagnostic(DLoc ? DLoc->getFilename() : "", SourceMgr::DK_Warning,
                        Entry->second.toString());
    Diag.print(ProgName, errs(), 1, 1);
  }
}

AnalysisKey InterproceduralAnalysis::Key;

InterproceduralAnalysis::Result
InterproceduralAnalysis::run(Module &M, ModuleAnalysisManager &AM) {
  TimeTraceScope TTS("ParcoachAnalysisInterAnalysis");
  auto const &PTACG = AM.getResult<PTACallGraphAnalysis>(M);
  auto &DG = AM.getResult<DepGraphDCFAnalysis>(M);
  LLVM_DEBUG(dbgs() << "Running PARCOACH InterproceduralAnalysis\n");
  auto PAInter = std::make_unique<ParcoachAnalysisInter>(M, DG.get(), *PTACG,
                                                         AM, EmitDotDG_);
  PAInter->run();
  return PAInter;
}

/*
 * INSTRUMENTATION
 */

#if 0
void ParcoachAnalysisInter::insertCountColl(llvm::Instruction *I,
                                            std::string OP_name, int OP_line,
                                            llvm::StringRef File, int inst) {

  IRBuilder<> builder(I);
  // Arguments of the new function
  std::vector<const Type *> params{};
  params.push_back(PointerType::getInt8PtrTy(M.getContext())); // OP_name
  Value *strPtr_NAME = builder.CreateGlobalStringPtr(OP_name);
  params.push_back(Type::getInt32Ty(M.getContext()));          // OP_line
  params.push_back(PointerType::getInt8PtrTy(M.getContext())); // FILE_name
  const std::string Filename = File.str();
  Value *strPtr_FILENAME = builder.CreateGlobalStringPtr(Filename);
  params.push_back(Type::getInt32Ty(M.getContext())); // inst
  // Set new function name, type and arguments
  FunctionType *FTy = FunctionType::get(
      Type::getVoidTy(M.getContext()),
      ArrayRef<Type *>((Type **)params.data(), params.size()), false);
  Value *CallArgs[] = {
      strPtr_NAME, ConstantInt::get(Type::getInt32Ty(M.getContext()), OP_line),
      strPtr_FILENAME,
      ConstantInt::get(Type::getInt32Ty(M.getContext()), inst)};
  std::string FunctionName = "count_collectives";
  FunctionCallee CCFunction = M.getOrInsertFunction(FunctionName, FTy);
  // Create new function
  CallInst::Create(CCFunction, ArrayRef<Value *>(CallArgs), "", I);
}
#endif
} // namespace parcoach
