#include "Concatenator.h"

using namespace std;

void concatenante(set<string>*res, set<string> *left, set<string> *right) {
    for (auto left_word : *left) {
        for(auto right_string : *right) {
            res -> insert(left_word + right_string);
        }
    }
}

void concatenate_insitu(std::set<std::string> *res, std::set<std::string> *right) {
    set<string> temp;
    for (auto left_string : *res) {
        for(auto right_string : *right) {
            temp . insert(left_string + right_string);
        }
    }
    res -> swap(temp);
}