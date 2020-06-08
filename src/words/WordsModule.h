#ifndef WORDS_MODULE_H
#define WORDS_MODULE_H

#include "../aSSA/PTACallGraph.h"

class WordsModule
{
private:
    PTACallGraph *PTACG;

    void dbg(llvm::Function*) const;
public:
    WordsModule(PTACallGraph *PTACG);
    ~WordsModule();

    void run();
};

#endif//WORDS_MODULE_H