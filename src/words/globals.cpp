#include "globals.h"


#include <map>
#include <llvm/IR/Function.h>
#include <set>
#include <string>
#include "WordsFunction.h"

std::map<llvm::Function *, std::set<std::string>> fun2set;
CollectiveRepresentation coll_repr;
AllWordsPass *pass;