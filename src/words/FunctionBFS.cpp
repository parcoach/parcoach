#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

#include "FunctionBFS.h"

using namespace llvm;

FunctionBFS::FunctionBFS(llvm::Function *function): browsed(function), curr(NULL), end_node(NULL), unvisited(), complete() {
    setup();
}

FunctionBFS::~FunctionBFS() {
}

void FunctionBFS::operator++() {
    unvisited.pop();
    complete[curr] = BLACK;
    pred_iterator P_curr = pred_begin(curr), P_end = pred_end(curr);
    for(;P_curr != P_end; ++P_curr) {
        auto bb = *P_curr;
        unvisited.push(bb);
    }
    curr = unvisited.front();
    if (mustWait(curr)) {
        unvisited.push(curr);
        curr = unvisited.front();
    }
    complete[curr] = GREY;
}

llvm::BasicBlock* FunctionBFS::operator*() {
    return curr;
}

bool FunctionBFS::isExitNode(llvm::BasicBlock* BB) {
    if(isa<ReturnInst>(BB -> getTerminator())) {
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
        /*if (f->getName().equals("MPI_Finalize") ||
            f->getName().equals("MPI_Abort") || f->getName().equals("abort")) {
        return true;
        }*/
    }
    return false;
}

void FunctionBFS::setup() {
    while(!unvisited.empty()) {unvisited.pop();}

    end_node = &browsed -> getEntryBlock();
    for(auto &BB : *browsed) {
        complete[&BB] = WHITE;
        if(isExitNode(&BB)) {
            unvisited.push(&BB);
            complete[&BB] = GREY;
            curr = first = &BB;
        }
    }
    //errs() << "Queue is empty ? " << unvisited.empty() << "\n";
}

bool FunctionBFS::mustWait(BasicBlock* bloc) {
    succ_iterator P_curr = succ_begin(bloc), P_end = succ_end(bloc);
    for(;P_curr != P_end; ++P_curr) {
        auto bb = *P_curr;
        if (complete[bb] != BLACK) {
            return true;
        }
    }
    return false;
}