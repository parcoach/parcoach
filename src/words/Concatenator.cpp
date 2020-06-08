#include "Concatenator.h"

using namespace std;

void concatenante(set<string>*res, set<string> *left, set<string> *right) {
    for (auto left_word : *left) {
        for(auto right_string : *right) {
            res -> insert(left_word + right_string);
        }
    }
}

void concatenate_insitu(std::set<std::string> *left, std::set<std::string> *res) {
    set<string> temp;
    for (auto left_string : *left) {
        for(auto right_string : *res) {
            temp . insert(left_string + right_string);
        }
    }
    res -> swap(temp);
}