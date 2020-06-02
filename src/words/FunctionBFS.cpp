#include "FunctionBFS.h"

FunctionBFS::FunctionBFS(llvm::Function *function): browsed(function), curr(NULL), end_node(NULL)
{
    for(auto &BB : *browsed) {
        
    }
    end_node;
}

FunctionBFS::~FunctionBFS()
{
}

iterator FunctionBFS::begin() {

}

const_iterator FunctionBFS::begin() const {

}

iterator FunctionBFS::end() {

}

const_iterator FunctionBFS::end() const {

}

void FunctionBFS::operator++() {

}

llvm::BasicBlock* FunctionBFS::operator*() {

}