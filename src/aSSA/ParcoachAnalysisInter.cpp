#include "ParcoachAnalysisInter.h"
#include "Collectives.h"
#include "Options.h"
#include "Utils.h"

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
using namespace std;

int ParcoachAnalysisInter::id = 0;

int nbColNI = 0;

void ParcoachAnalysisInter::run() {
  // Parcoach analysis

  /* (1) BFS on each function of the Callgraph in reverse topological order
   *  -> set a function summary with sequence of collectives
   *  -> keep a set of collectives per BB and set the conditionals at NAVS if
   *     it can lead to a deadlock
   */
  LLVM_DEBUG(dbgs() << " (1) BFS\n");
  scc_iterator<PTACallGraph *> cgSccIter = scc_begin(&PTACG);
  while (!cgSccIter.isAtEnd()) {
    const vector<PTACallGraphNode *> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
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
    const vector<PTACallGraphNode *> &nodeVec = *cgSccIter;
    for (PTACallGraphNode *node : nodeVec) {
      Function *F = node->getFunction();
      if (!F || F->isDeclaration() || !PTACG.isReachableFromEntry(F))
        continue;
      // DBG: //errs() << "Function: " << F->getName() << "\n";
      checkCollectives(F);
      // Get the number of MPI communicators
      if (optMpiTaint) {
        // errs() << " ... Found " << mpiCollListperFuncMap[F].size() << " MPI
        // communicators in " << F->getName() << "\n";
        /*for(auto& pair : mpiCollListperFuncMap[F]){
                errs() << pair.first << "{" << pair.second << "}\n";
                }*/
      }
    }
    ++cgSccIter;
  }
  /*
          if(nbWarnings !=0){
          cgSccIter = scc_begin(&PTACG);
          while(!cgSccIter.isAtEnd()) {
                  const vector<PTACallGraphNode*> &nodeVec = *cgSccIter;
                  for (PTACallGraphNode *node : nodeVec) {
                          Function *F = node->getFunction();
                          if (!F || F->isDeclaration() ||
     !PTACG.isReachableFromEntry(F))
                                  continue;
                          countCollectivesToInst(F);
                  }
                  ++cgSccIter;
          }
          }
  */
  // If you always want to instrument the code, uncomment the following line
  // if(nbWarnings !=0){
  if (nbWarnings != 0 && !disableInstru) {
    LLVM_DEBUG(
        dbgs()
        << "\033[0;35m=> Static instrumentation of the code ...\033[0;0m\n");
    for (Function &F : M) {
      instrumentFunction(&F);
    }
  }
  //	errs() << "Number of collectives to instrument = " << nbColNI << "\n";
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
        for (const Function *mayCallee : PTACG.indirectCallMap[inst]) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          callee = const_cast<Function *>(mayCallee);
          // Is it a function containing collectives?
          if (!collperFuncMap[callee].empty()) // && collMap[BB]!="NAVS")
            collMap[BB] = collperFuncMap[callee] + " " + collMap[BB];
          // Is it a collective operation?
          if (isCollective(callee)) {
            string OP_name = callee->getName().str();
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
        if (isCollective(callee)) {
          string OP_name = callee->getName().str();
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
        for (const Function *mayCallee : PTACG.indirectCallMap[inst]) {
          if (isIntrinsicDbgFunction(mayCallee))
            continue;
          callee = const_cast<Function *>(mayCallee);
          // Is it a function containing collectives?
          if (!mpiCollListperFuncMap[callee]
                   .empty()) { // && mpiCollMap[BB]!="NAVS"){
            // auto &BBcollMap = mpiCollMap[BB];

            for (auto &pair : mpiCollListperFuncMap[callee]) {
              CollList *cl = mpiCollListMap[BB][pair.first];

              if (cl && cl->isSource(BB))
                cl->pushColl(pair.second);
              else if (!cl || !cl->isNAVS())
                mpiCollListMap[BB][pair.first] =
                    new CollList(pair.second, cl, BB);
            }
          }
          // Is it a collective operation?
          if (isCollective(callee)) {
            string OP_name = callee->getName().str();
            int OP_color = getCollectiveColor(callee);
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
              for (auto &pair : mpiCollListMap[BB]) {
                CollList *cl = mpiCollListMap[BB][pair.first];

                if (cl && cl->isSource(BB))
                  cl->pushColl(OP_name);
                else if (!cl || !cl->isNAVS())
                  mpiCollListMap[BB][pair.first] =
                      new CollList(OP_name, cl, BB);
              }
            }
          }
        }
        //// Direct calls
      } else {
        // Is it a function containing collectives?
        if (!mpiCollListperFuncMap[callee].empty()) {
          // errs() << "call to function " << callee->getName() << "\n";
          for (auto &pair : mpiCollListperFuncMap[callee]) {
            CollList *cl = mpiCollListMap[BB][pair.first];
            if (cl && cl->isSource(BB))
              cl->pushColl(pair.second);
            else if (!cl || !cl->isNAVS())
              mpiCollListMap[BB][pair.first] =
                  new CollList(pair.second, cl, BB);
          }
        }
        // Is it a collective operation?
        if (isCollective(callee)) {
          string OP_name = callee->getName().str();
          int OP_color = getCollectiveColor(callee);
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
            for (auto &pair : mpiCollListMap[BB]) {
              CollList *cl = mpiCollListMap[BB][pair.first];
              if (cl && cl->isSource(BB))
                cl->pushColl(OP_name);
              else if (!cl || !cl->isNAVS())
                mpiCollListMap[BB][pair.first] = new CollList(OP_name, cl, BB);
            }
          }
        }
      }
    }
  }
}

// Tag loop preheader
void ParcoachAnalysisInter::Tag_LoopLatches(llvm::Loop *L) {
  BasicBlock *Lheader = L->getHeader();
  if (L->getNumBlocks() == 1)
    return; // header=preheader when the loop has only one node, no need for a
            // particular case

  pred_iterator PI = pred_begin(Lheader), E = pred_end(Lheader);
  for (; PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (L->contains(Pred))
      bbLatchesMap[Pred] = true;
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
        for (auto &com : mpiCollListMap[header])
          mpiCollListMap[Pred][com.first] = mpiCollListMap[header][com.first];

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
  for (auto &com : mpiCollListMap[Lheader]) {
    if (mpiCollListMap[Lheader][com.first] &&
        mpiCollListMap[Lheader][com.first]->isSource(Lheader))
      mpiCollListMap[Lheader][com.first]->pushColl("NAVS");
    else
      mpiCollListMap[Lheader][com.first] =
          new CollList("NAVS", mpiCollListMap[Lheader][com.first], Lheader);
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
        for (auto &com : mpiCollListMap[header])
          mpiCollListMap[Pred][com.first] = mpiCollListMap[header][com.first];

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
  for (auto &pair : mpiCollListMap[&entry]) {
    // errs() << " CollList: " << F->getName().str() << " : "
    //        << mpiCollListMap[&entry][pair.first]->toString() << "\n";
    mpiCollListperFuncMap[F][pair.first] = mpiCollListMap[&entry][pair.first];
  }
  /*if(F->getName() == "main"){
          errs() << F->getName() << " summary = \n";
          for(auto& pair : mpiCollListperFuncMap[F]){
                  errs() << pair.first << "{" << pair.second << "}\n";
          }
  }*/
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
        string temp = collMap[Pred];
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

  // DEBUG//errs() << "-- BFS --\n";
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
  // DEBUG//errs() << "-- BFS IN EACH LOOP --\n";
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
        string temp = collMap[Pred];
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
}

// COUNT COLLECTIVES TO INST AND INSTRUMENT THEM
// Not correct..
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
    string OP_name = f->getName().str();
    string Warning = getWarning(*i);
    DebugLoc locC = i->getDebugLoc();
    int OP_line = 0;
    string File = "";
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
    set<const BasicBlock *> callIPDF;
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

// CHECK COLLECTIVES FUNCTION
void ParcoachAnalysisInter::checkCollectives(llvm::Function *F) {
  using Warning = pair<unsigned, string>;
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *i = &*I;
    // Debug info (line in the source code, file)
    DebugLoc DLoc = i->getDebugLoc();
    StringRef File = "";
    unsigned OP_line = 0;
    if (DLoc) {
      OP_line = DLoc.getLine();
      File = DLoc->getFilename();
    }
    // Warning info
    const char *ProgName = "PARCOACH";
    SMDiagnostic Diag;
    vector<Warning> Warnings;

    CallInst *CI = dyn_cast<CallInst>(i);
    if (!CI)
      continue;

    Function *f = CI->getCalledFunction();
    if (!f)
      continue;

    StringRef OP_name = f->getName();

    // Is it a collective call?
    if (!isCollective(f)) {
      continue;
    }

    int OP_color = getCollectiveColor(f);
    Value *OP_com = nullptr;
    int OP_arg_id = -1;

    if (optMpiTaint) {
      OP_arg_id = Com_arg_id(OP_color);
      if (OP_arg_id >= 0)
        OP_com = CI->getArgOperand(OP_arg_id); // 0 for Barrier only
    }

    // CI->getArgOperand(0)->dump();
    // errs() << "Found " << OP_name << " on " << OP_com << " line " << OP_line
    // << " in " << File << "\n";

    nbCollectivesFound++;
    bool isColWarning = false;
    bool isColWarningParcoach = false;

    // Get conditionals from the callsite
    set<const BasicBlock *> callIPDF;
    DG->getCallInterIPDF(CI, callIPDF);
    // For the summary-based approach, use the following instead of the previous
    // line
    // DG->getCallIntraIPDF(CI, callIPDF);

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
      /*const Value *cond = getBasicBlockCond(BB);
      errs() << "Cond : " << cond->getName() << "\n";
            for(auto& pair : mpiCollMap[BB]){
errs() << pair.first << "{" << pair.second << "}\n";}*/

      isColWarningParcoach = true;
      nbCondsParcoachOnly++;
      conditionSetParcoachOnly.insert(BB);

      // Is this condition tainted?
      const Value *cond = getBasicBlockCond(BB);

      /*errs() << "Cond with NAVS: " << cond->getName() << "\n";

for(auto& pair : mpiCollMap[BB]){
errs() << pair.first << "{" << pair.second << "}\n";
}
      */

      if (!cond || (!optNoDataFlow && !DG->isTaintedValue(cond))) {
        const Instruction *instE = BB->getTerminator();
        DebugLoc locE = instE->getDebugLoc();
        // errs() << "-> not tainted\n";
        continue;
      }

      isColWarning = true;
      nbConds++;
      conditionSet.insert(BB);

      DebugLoc BDLoc = (BB->getTerminator())->getDebugLoc();
      const Instruction *inst = BB->getTerminator();
      DebugLoc loc = inst->getDebugLoc();
      Warnings.push_back(make_pair(loc ? loc.getLine() : 0,
                                   loc ? loc->getFilename().str() : "?"));

      if (optDotTaintPaths) {
        string dotfilename("taintedpath-");
        string cfilename = loc->getFilename().str();
        size_t lastpos_slash = cfilename.find_last_of('/');
        if (lastpos_slash != cfilename.npos)
          cfilename = cfilename.substr(lastpos_slash + 1, cfilename.size());
        dotfilename.append(cfilename).append("-");
        dotfilename.append(to_string(loc.getLine())).append(".dot");
        DG->dotTaintPath(cond, dotfilename, i);
      }
    } // END FOR

    // Is there at least one node from the IPDF+ detected as potentially
    // dangerous by parcoach
    if (isColWarningParcoach) {
      nbWarningsParcoachOnly++;
      warningSetParcoachOnly.insert(CI);
    }

    // Is there at least one node from the IPDF+ tainted
    if (!isColWarning)
      continue;

    nbWarnings++;
    warningSet.insert(CI);
    std::string Buf;
    raw_string_ostream WarningMsg(Buf);
    WarningMsg << OP_name << " line " << OP_line;
    WarningMsg << " possibly not called by all processes because of "
                  "conditional(s) line(s) ";
    // Make sure lines are displayed in order.
    std::sort(Warnings.begin(), Warnings.end());
    for (auto &W : Warnings) {
      WarningMsg << " " << (W.first ? to_string(W.first) : "?");
      WarningMsg << " (" << W.second << ")";
    }
    WarningMsg << " (full-inter)";

    MDNode *mdNode = MDNode::get(
        i->getContext(), MDString::get(i->getContext(), WarningMsg.str()));
    i->setMetadata("inter.inst.warning" + to_string(id), mdNode);
    Diag = SMDiagnostic(File, SourceMgr::DK_Warning, WarningMsg.str());
    Diag.print(ProgName, errs(), 1, 1);
  }
}

/*
 * INSTRUMENTATION
 */

void ParcoachAnalysisInter::instrumentFunction(llvm::Function *F) {
  for (Function::iterator bb = F->begin(), e = F->end(); bb != e; ++bb) {
    for (BasicBlock::iterator i = bb->begin(), e = bb->end(); i != e; ++i) {
      Instruction *Inst = &*i;
      string Warning = getWarning(*Inst);
      // Debug info (line in the source code, file)
      DebugLoc DLoc = i->getDebugLoc();
      string File = "o";
      int OP_line = -1;
      if (DLoc) {
        OP_line = DLoc.getLine();
        File = DLoc->getFilename().str();
      }
      // call instruction
      if (CallInst *CI = dyn_cast<CallInst>(i)) {
        Function *callee = CI->getCalledFunction();
        if (callee == NULL)
          continue;
        string OP_name = callee->getName().str();
        int OP_color = getCollectiveColor(callee);

        // Before finalize or exit/abort
        if (callee->getName().equals("MPI_Finalize") ||
            callee->getName().equals("MPI_Abort") ||
            callee->getName().equals("abort")) {
          LLVM_DEBUG(dbgs() << "-> insert check before " << OP_name << " line "
                            << OP_line << "\n");
          insertCC(Inst, v_coll.size() + 1, OP_name, OP_line, Warning, File);
          // nbCC++;
          continue;
        }
        // Before a collective
        if (OP_color >= 0) {
          LLVM_DEBUG(dbgs() << "-> insert check before " << OP_name << " line "
                            << OP_line << "\n");
          insertCC(Inst, OP_color, OP_name, OP_line, Warning, File);
          // nbCC++;
        }
      } // END IF
    }   // END FOR
  }     // END FOR
}

void ParcoachAnalysisInter::insertCountColl(llvm::Instruction *I,
                                            std::string OP_name, int OP_line,
                                            llvm::StringRef File, int inst) {

  IRBuilder<> builder(I);
  // Arguments of the new function
  vector<const Type *> params = vector<const Type *>();
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

// Check Collective function before a collective
// + Check Collective function before return statements
// --> check_collective_MPI(int OP_color, const char* OP_name, int OP_line,
// char* OP_warnings, char *FILE_name)
// --> void check_collective_UPC(int OP_color, const char* OP_name,
// int OP_line, char* warnings, char *FILE_name)
void ParcoachAnalysisInter::insertCC(llvm::Instruction *I, int OP_color,
                                     std::string OP_name, int OP_line,
                                     llvm::StringRef WarningMsg,
                                     llvm::StringRef File) {

  IRBuilder<> builder(I);
  // Arguments of the new function
  vector<const Type *> params = vector<const Type *>();
  params.push_back(Type::getInt32Ty(M.getContext()));          // OP_color
  params.push_back(PointerType::getInt8PtrTy(M.getContext())); // OP_name
  Value *strPtr_NAME = builder.CreateGlobalStringPtr(OP_name);
  params.push_back(Type::getInt32Ty(M.getContext()));          // OP_line
  params.push_back(PointerType::getInt8PtrTy(M.getContext())); // OP_warnings
  const std::string Warnings = WarningMsg.str();
  Value *strPtr_WARNINGS = builder.CreateGlobalStringPtr(Warnings);
  params.push_back(PointerType::getInt8PtrTy(M.getContext())); // FILE_name
  const std::string Filename = File.str();
  Value *strPtr_FILENAME = builder.CreateGlobalStringPtr(Filename);
  // Set new function name, type and arguments
  FunctionType *FTy = FunctionType::get(
      Type::getVoidTy(M.getContext()),
      ArrayRef<Type *>((Type **)params.data(), params.size()), false);
  Value *CallArgs[] = {
      ConstantInt::get(Type::getInt32Ty(M.getContext()), OP_color), strPtr_NAME,
      ConstantInt::get(Type::getInt32Ty(M.getContext()), OP_line),
      strPtr_WARNINGS, strPtr_FILENAME};
  std::string FunctionName;

  if (isMpiCollective(OP_color)) {
    FunctionName = "check_collective_MPI";
  } else if (isOmpCollective(OP_color)) {
    FunctionName = "check_collective_OMP";
  } else if (isUpcCollective(OP_color)) {
    FunctionName = "check_collective_UPC";
  } else {
    FunctionName = "check_collective_return";
  }

  FunctionCallee CCFunction = M.getOrInsertFunction(FunctionName, FTy);

  // Create new function
  CallInst::Create(CCFunction, ArrayRef<Value *>(CallArgs), "", I);
  LLVM_DEBUG(dbgs() << "=> Insertion of " << FunctionName << " (" << OP_color
                    << ", " << OP_name << ", " << OP_line << ", " << WarningMsg
                    << ", " << File << ")\n");
}

std::string ParcoachAnalysisInter::getWarning(llvm::Instruction &inst) {
  string warning = " ";
  if (MDNode *node = inst.getMetadata("inter.inst.warning" + to_string(id))) {
    if (Metadata *value = node->getOperand(0)) {
      MDString *mdstring = cast<MDString>(value);
      warning = mdstring->getString().str();
    }
  } else {
    // errs() << "Did not find metadata\n";
  }
  return warning;
}
