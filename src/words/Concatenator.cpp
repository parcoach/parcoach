#include "Concatenator.h"

using namespace std;

set<string>* concatenante(std::set<std::string> *left, std::set<std::string> *right) {
    set<string> *res = new set<string>();
    for (auto left_word : *left) {
        for(auto right_string : *right) {
            res -> insert(left_word + right_string);
        }
    }
    return res;
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