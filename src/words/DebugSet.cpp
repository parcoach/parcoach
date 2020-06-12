#include <set>
#include <string>
#include <llvm/Support/raw_ostream.h>
#include "Word.h"

using namespace std;
using namespace llvm;

void print_set(std::set<string> const& set_str) {
    errs() << "{ ";
    int cpt = 0;
    for (auto s : set_str) {
        errs() << "\"" << s.substr(0, s.find_last_of("-")-1) << "\"";
        if (++ cpt < set_str.size()) {errs() << ", ";}
    }
    errs() << " }\n";
}