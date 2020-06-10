#ifndef WORD_H
#define WORD_H

#include <vector>
#include <string>

#if 0
class Word
{
private:
    /* data */
    std::vector<std::string> collectives;
public:
    Word();
    ~Word();
};
#else
typedef std::vector<std::string> Word;
#endif

#endif//WORD_H