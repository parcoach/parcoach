#ifndef GLOBALS_H
#define GLOBALS_H

#include <map>

#include <llvm/IR/Function.h>
#include <set>
#include <string>
#include "CollectiveRepresentation.h"
#include "AllWordsPass.h"
//#include "WordsFunction.h"

extern std::map<llvm::Function *, std::set<std::string>> fun2set;
extern CollectiveRepresentation coll_repr;
extern AllWordsPass *pass;

#endif//GLOBALS_H