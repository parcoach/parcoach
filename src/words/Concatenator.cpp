#include "Concatenator.h"
#include <llvm/Support/raw_ostream.h>
using namespace std;

void concatenante(set<string>*res, set<string> *left, set<string> *right) {
    for (auto left_word : *left) {
        for(auto right_string : *right) {
            if(left_word.find("exit") == string::npos) {
                res -> insert(left_word + right_string);
            } else {
                res -> insert(left_word);
            }
        }
    }
}

void concatenate_insitu(std::set<std::string> *res, std::set<std::string> *right) {
    set<string> temp;
    for (auto left_string : *res) {
        for(auto right_string : *right) {
            if(left_string.find("exit") == string::npos) {
                temp . insert(left_string + right_string);
            } else {
                llvm::errs() << left_string << "\n";
                temp . insert(left_string);
            }
        }
    }
    res -> swap(temp);
}