#ifndef GLOBALS_H
#define GLOBALS_H

#include <map>

#include <llvm/IR/Function.h>
#include <set>
#include <string>
//#include "WordsFunction.h"

extern std::map<llvm::Function *, std::set<std::string>> fun2set;

#endif//GLOBALS_H