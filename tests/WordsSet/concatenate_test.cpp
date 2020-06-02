#include <set>
#include <string>
#include <iostream>

using namespace std;

void concatenante_insitu(std::set<std::string> *res, std::set<std::string> *right) {
    for(string res_string : *res) {
        for(string right_string : *right) {
            res_string += right_string;
        }
    }
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

    concatenante_insitu(&left, &right);

    cout << (left == expected) << endl;

    return 0;
}
