#include "Concatenator.h"
#include <set>
#include <string>
#include <iostream>

using namespace std;

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