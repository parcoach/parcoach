#include "Concatenator.h"

using namespace std;

void concatenante_insitu(std::set<std::string> *res, std::set<std::string> *right) {
    for(string res_string : *res) {
        for(string right_string : *right) {
            res_string += right_string;
        }
    }
}
