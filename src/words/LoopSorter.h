#ifndef LOOP_SORTER_H
#define LOOP_SORTER_H

#include <vector>
#include <llvm/Analysis/LoopInfo.h>

bool compare_loop(llvm::Loop *left, llvm::Loop *right);

#endif//LOOP_SORTER_H