#ifndef FUNCTION_BFS_H
#define FUNCTION_BFS_H

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h> 
#include <queue>
#include <map>

typedef llvm::BasicBlock* iterator;
typedef llvm::BasicBlock const* const_iterator;

enum Color {WHITE, GREY, BLACK};

class FunctionBFS
{
private:
    llvm::Function *browsed;
    llvm::BasicBlock *first;
    llvm::BasicBlock *curr;
    llvm::BasicBlock *end_node;
    /* */
    std::queue<llvm::BasicBlock*> unvisited;
    /* To say if a node is completly visited (e.g., all its next nodes have been visited) */
    std::map<llvm::BasicBlock*, Color> complete;
    /* data */

    bool mustWait(llvm::BasicBlock*);
    void reset();
    bool isExitNode(llvm::BasicBlock*);
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