#include <set>
#include <string>
#include <iostream>
#include <memory>

using namespace std;

void concatenate_insitu(std::set<std::string> *left, std::set<std::string> *res) {
    set<string> temp;
    for (auto left_string : *left) {
        for(auto right_string : *res) {
            temp . insert(left_string + right_string);
        }
    }
    res -> swap(temp);
}

int main(int argc, char **argv) {
    set<string> left, right, expected;

    left.insert("B");
    left.insert("BB");
    left.insert("BBBB");

    right.insert("A");
    right.insert("AB");
    right.insert("ABAB");

    expected.insert("BA");
    expected.insert("BAB");
    expected.insert("BABAB");
    expected.insert("BBA");
    expected.insert("BBAB");
    expected.insert("BBABAB");
    expected.insert("BBBBA");
    expected.insert("BBBBAB");
    expected.insert("BBBBABAB");

    concatenate_insitu(&left, &right);

    cout << "Concatenation working : " << (right == expected) << endl;

    return 0;
}
