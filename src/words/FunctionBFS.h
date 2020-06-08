#ifndef FUNCTION_BFS_H
#define FUNCTION_BFS_H

#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h> 
#include <queue>
#include <map>

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
    void setup();
    bool isExitNode(llvm::BasicBlock*);
public:

    typedef llvm::BasicBlock* iterator;
    typedef llvm::BasicBlock const* const_iterator;

    iterator begin() {return first;}
    const_iterator begin() const {return first;}
    iterator end() {return end_node;}
    const_iterator end() const {return end_node;}

    void operator++();
    llvm::BasicBlock* operator*();

    FunctionBFS(llvm::Function *function);
    ~FunctionBFS();
};


#endif//FUNCTION_BFS_H