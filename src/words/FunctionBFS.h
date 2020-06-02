#ifndef FUNCTION_BFS_H
#define FUNCTION_BFS_H

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h> 

typedef llvm::BasicBlock* iterator;
typedef llvm::BasicBlock const* const_iterator;

class FunctionBFS
{
private:
    llvm::Function *browsed;
    llvm::BasicBlock *curr;
    llvm::BasicBlock *end_node;
    /* data */
public:

    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;

    void operator++();
    llvm::BasicBlock* operator*();

    FunctionBFS(llvm::Function *function);
    ~FunctionBFS();
};


#endif//FUNCTION_BFS_H