#ifndef CONCATENATOR_H
#define CONCATENATOR_H

#include <set>
#include <string>

void concatenante(std::set<std::string>*res, std::set<std::string> *left, std::set<std::string> *right);

/**
 * concatenate two set of string, storing the result in the first argument
 */
void concatenate_insitu(std::set<std::string> *res, std::set<std::string> *right);

#endif//CONCATENATOR_H