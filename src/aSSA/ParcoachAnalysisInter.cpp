#include "ParcoachAnalysisInter.h"
#include "Collectives.h"
#include "Instrumentation.h"
#include "Options.h"
#include "Utils.h"
#include "parcoach/DepGraphDCF.h"

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

      if (optMpiTaint)
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

/*
 * FUNCTIONS USED TO CHECK COLLECTIVES
 */

// Get the sequence of collectives in a BB
void ParcoachAnalysisInter::setCollSet(BasicBlock *BB) {
  for (auto i = BB->rbegin(), e = BB->rend(); i != e; ++i) {
    const Instruction *inst = &*i;

    // errs() << "call? " << dyn_cast<CallInst>(inst) << "\n";

    if (const CallInst *CI = dyn_cast<CallInst>(inst)) {
      Function *callee = CI->getCalledFunction();

      // if (callee != NULL)
      //   errs() << "collective? " << callee->getName() << "\n";

      //// Indirect calls
      if (callee == NULL) {
        for (const Function *mayCallee :
             PTACG.getIndirectCallMap().lookup(inst)) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          callee = const_cast<Function *>(mayCallee);
          // Is it a function containing collectives?
          if (!collperFuncMap[callee].empty()) // && collMap[BB]!="NAVS")
            collMap[BB] = collperFuncMap[callee] + " " + collMap[BB];
          // Is it a collective operation?
          if (isCollective(*callee)) {
            std::string OP_name = callee->getName().str();
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
          if (collperFuncMap[callee] == "NAVS" || collMap[BB] == "NAVS")
            collMap[BB] = "NAVS";
          else
            collMap[BB] = collperFuncMap[callee] + " " + collMap[BB];
        }
        // Is it a collective operation?
        if (isCollective(*callee)) {
          std::string OP_name = callee->getName().str();
          if (collMap[BB].empty())
            collMap[BB] = OP_name;
          else
            collMap[BB] = OP_name + " " + collMap[BB];
        }
      }
    }
  }
}

// Get the sequence of collectives in a BB, per MPI communicator
void ParcoachAnalysisInter::setMPICollSet(BasicBlock *BB) {
  for (auto i = BB->rbegin(), e = BB->rend(); i != e; ++i) {
    const Instruction *inst = &*i;
    if (const CallInst *CI = dyn_cast<CallInst>(inst)) {
      Function *callee = CI->getCalledFunction();

      //// Indirect call
      if (callee == NULL) {
        LLVM_DEBUG(dbgs() << "Indirect call map size: "
                          << PTACG.getIndirectCallMap().lookup(inst).size()
                          << "\n");
        for (const Function *mayCallee :
             PTACG.getIndirectCallMap().lookup(inst)) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          callee = const_cast<Function *>(mayCallee);
          LLVM_DEBUG(dbgs() << "Callee: " << callee->getName()
                            << ", contains collective: "
                            << mpiCollListperFuncMap[callee].size() << "\n");
          // Is it a function containing collectives?
          if (!mpiCollListperFuncMap[callee]
                   .empty()) { // && mpiCollMap[BB]!="NAVS"){
            // auto &BBcollMap = mpiCollMap[BB];

            for (auto &[Val, List] : mpiCollListperFuncMap[callee]) {
              CollList *cl = mpiCollListMap[BB][Val];

              if (cl && cl->isSource(BB))
                cl->pushColl(List);
              else if (!cl || !cl->isNAVS())
                mpiCollListMap[BB][Val] = new CollList(List, cl, BB);
            }
          }
          // Is it a collective operation?
          if (isCollective(*callee)) {
            std::string OP_name = callee->getName().str();
            int OP_color = getCollectiveColor(*callee);
            int OP_arg_id = Com_arg_id(OP_color);
            Value *OP_com = nullptr;

            if (OP_arg_id >= 0) {
              OP_com = CI->getArgOperand(OP_arg_id);
              CollList *cl = mpiCollListMap[BB][OP_com];

              if (cl && cl->isSource(BB))
                cl->pushColl(OP_name);
              else if (!cl || !cl->isNAVS())
                mpiCollListMap[BB][OP_com] = new CollList(OP_name, cl, BB);
            } else {
              // Case of collective are effect on all com (ex: MPI_Finalize)
              for (auto &[Val, List] : mpiCollListMap[BB]) {
                if (List && List->isSource(BB))
                  List->pushColl(OP_name);
                else if (!List || !List->isNAVS())
                  List = new CollList(OP_name, List, BB);
              }
            }
          }
        }
        //// Direct calls
      } else {
        // Is it a function containing collectives?
        for (auto &[Val, List] : mpiCollListperFuncMap[callee]) {
          CollList *cl = mpiCollListMap[BB][Val];
          if (cl && cl->isSource(BB))
            cl->pushColl(List);
          else if (!cl || !cl->isNAVS())
            mpiCollListMap[BB][Val] = new CollList(List, cl, BB);
        }
        // Is it a collective operation?
        if (isCollective(*callee)) {
          std::string OP_name = callee->getName().str();
          int OP_color = getCollectiveColor(*callee);
          int OP_arg_id = Com_arg_id(OP_color);
          Value *OP_com = nullptr;

          if (OP_arg_id >= 0) {
            OP_com = CI->getArgOperand(OP_arg_id);
            CollList *cl = mpiCollListMap[BB][OP_com];

            if (cl && cl->isSource(BB))
              cl->pushColl(OP_name);
            else
              mpiCollListMap[BB][OP_com] = new CollList(OP_name, cl, BB);
          } else {
            // Case of collective are effect on all com (ex: MPI_Finalize)
            for (auto &[Val, List] : mpiCollListMap[BB]) {
              if (List && List->isSource(BB))
                List->pushColl(OP_name);
              else if (!List || !List->isNAVS())
                List = new CollList(OP_name, List, BB);
            }
          }
        }
      }
    }
  }
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
  SetVector<const Value *> comm;

  for (auto &com : mpiCollListMap[pred])
    comm.insert(com.first);

  for (auto &com : mpiCollListMap[header])
    comm.insert(com.first);

  for (auto &com : comm) {
    bool pred_owner = false;
    CollList *pred_cl = mpiCollListMap[pred][com];
    CollList *old_header_cl = pred_cl;
    CollList *header_cl = mpiCollListMap[header][com];

    // Header and old_header are nullptr
    if (!old_header_cl && !header_cl) {
      continue;
    }

    // Update old_header if pred is the source of the list
    if (pred_cl && pred_cl->isSource(pred)) {
      old_header_cl = old_header_cl->getNext();
      pred_owner = true;
    }

    LLVM_DEBUG({
      dbgs() << " ** (oh == h) = "
             << ((old_header_cl && header_cl && (*old_header_cl == *header_cl))
                     ? "true"
                     : "false")
             << "\n"
             << "     p = " << (pred_cl ? pred_cl->toCollMap() : "") << "\n"
             << "    oh = " << (old_header_cl ? old_header_cl->toCollMap() : "")
             << "\n"
             << "     h = " << (header_cl ? header_cl->toCollMap() : "") << "\n"
             << "   fun = "
             << (header ? header->getParent()->getName().str()
                        : (pred ? pred->getParent()->getName().str() : "xxx"))
             << "\n";
    });

    // Equals
    if (old_header_cl && header_cl && (*old_header_cl == *header_cl))
      return;

    // Not equals
    if (pred_cl && !pred_cl->isNAVS()) {
      if (pred_owner)
        pred_cl->pushColl("NAVS");
      else
        mpiCollListMap[pred][com] = new CollList("NAVS", pred_cl, pred);
    }
  }
}

void ParcoachAnalysisInter::MPI_BFS_Loop(llvm::Loop *L) {
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
    BasicBlock *header = *Unvisited.begin();
    // DEBUG//errs() << "Header " << header->getName() << "\n";
    Unvisited.pop_front();

    if (bbVisitedMap[header] == black)
      continue;

    if (mustWaitLoop(header, L) &&
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
          mpiCollListMap[Pred][Val] = List;
        }

        setMPICollSet(Pred);
        bbVisitedMap[Pred] = grey;

        if (Pred != Lheader && (L->contains(Pred) || isExitNode(Pred)))
          Unvisited.push_back(Pred);
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
    if (List && List->isSource(Lheader)) {
      List->pushColl("NAVS");
    } else {
      List = new CollList("NAVS", List, Lheader);
    }
  }
}

// FOR MPI APPLIS: BFS
void ParcoachAnalysisInter::MPI_BFS(llvm::Function *F) {
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
          mpiCollListMap[Pred][Val] = List;
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

  BasicBlock &entry = F->getEntryBlock();
  for (auto &[Val, List] : mpiCollListMap[&entry]) {
    mpiCollListperFuncMap[F][Val] = List;
  }
}

// FOR BFS: Returns true if all successors are not black
bool ParcoachAnalysisInter::mustWait(llvm::BasicBlock *bb) {
  succ_iterator SI = succ_begin(bb), E = succ_end(bb);
  for (; SI != E; ++SI) {
    BasicBlock *Succ = *SI;
    if (bbVisitedMap[Succ] != black)
      return true;
  }
  return false;
}

bool ParcoachAnalysisInter::mustWaitLoop(llvm::BasicBlock *bb, Loop *l) {
  succ_iterator SI = succ_begin(bb), E = succ_end(bb);
  for (; SI != E; ++SI) {
    BasicBlock *Succ = *SI;
    if (bbVisitedMap[Succ] != black && l->contains(Succ))
      return true;
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

  // BFS
  Unvisited.push_back(Lheader); // Starting node

  while (Unvisited.size() > 0) {
    BasicBlock *header = *Unvisited.begin();
    // DEBUG//errs() << "Header " << header->getName() << "\n";
    Unvisited.pop_front();

    if (bbVisitedMap[header] == black)
      continue;

    if (mustWaitLoop(header, L) &&
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
        // XXX: Loop always set NAVS to avoid différent loop itérations
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
  auto IsaDirectCallToCollective = [](Instruction const &I) {
    if (CallInst const *CI = dyn_cast<CallInst>(&I)) {
      Function const *F = CI->getCalledFunction();
      return F && isCollective(*F);
    }
    return false;
  };
  auto Candidates =
      make_filter_range(instructions(F), IsaDirectCallToCollective);
  for (Instruction &I : Candidates) {
    // Debug info (line in the source code, file)
    // Warning info
    const char *ProgName = "PARCOACH";
    SMDiagnostic Diag;
    SmallVector<DebugLoc, 2> Conditionals;

    CallInst &CI = cast<CallInst>(I);
    Function &F = *CI.getCalledFunction();

    LLVM_DEBUG({
      dbgs() << "Running on ";
      CI.print(dbgs());
      dbgs() << "\n";
    });

    int OP_color = getCollectiveColor(F);
    Value *OP_com = nullptr;
    int OP_arg_id = -1;

    if (optMpiTaint) {
      OP_arg_id = Com_arg_id(OP_color);
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
    std::set<const BasicBlock *> callIPDF;
    DG->getCallInterIPDF(&CI, callIPDF);
    // For the summary-based approach, use the following instead of the previous
    // line
    // DG->getCallIntraIPDF(CI, callIPDF);
    LLVM_DEBUG(dbgs() << "callIPDF size: " << callIPDF.size() << "\n");
    if (!callIPDF.empty())
      nbCollectivesCondCalled++;

    for (const BasicBlock *BB : callIPDF) {
      // Is this node detected as potentially dangerous by parcoach?
      if (!optMpiTaint && collMap[BB] != "NAVS") {
        continue;
      }
      if (optMpiTaint && OP_arg_id >= 0 && mpiCollListMap[BB][OP_com] &&
          !mpiCollListMap[BB][OP_com]->isNAVS()) {
        continue;
      }

      isColWarningParcoach = true;
      conditionSetParcoachOnly.insert(BB);

      // Is this condition tainted?
      const Value *cond = getBasicBlockCond(BB);

      if (!cond || (!optNoDataFlow && !DG->isTaintedValue(cond))) {
        const Instruction *instE = BB->getTerminator();
        DebugLoc locE = instE->getDebugLoc();
        // errs() << "-> not tainted\n";
        continue;
      }

      isColWarning = true;
      conditionSet.insert(BB);

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      const Instruction *inst = BB->getTerminator();
      DebugLoc loc = inst->getDebugLoc();
      Conditionals.push_back(loc);

      if (optDotTaintPaths) {
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
  auto const &PTACG = AM.getResult<PTACallGraphAnalysis>(M);
  auto &DG = AM.getResult<DepGraphDCFAnalysis>(M);
  LLVM_DEBUG(dbgs() << "Running PARCOACH InterproceduralAnalysis\n");
  auto PAInter = std::make_unique<ParcoachAnalysisInter>(M, DG.get(), *PTACG,
                                                         AM, !optInstrumInter);
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
