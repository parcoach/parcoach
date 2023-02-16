#ifndef COLLLIST_H
#define COLLLIST_H

#include <set>
#include <vector>

#include "llvm/IR/Module.h"

// FIXME: leaks -> unique_ptr somewhere
class CollList {
  bool Navs;

  llvm::SmallVector<llvm::BasicBlock const *, 4> Sources;
  std::vector<std::string> Names;

public:
  // Construct new list node
  CollList(llvm::StringRef Coll, llvm::BasicBlock const *Src);
  CollList(CollList const *Coll, llvm::BasicBlock const *Src);
  CollList() = default;
  CollList(CollList const &) = default;
  ~CollList() = default;

  bool isNAVS() const { return Navs; }
  bool isSource(llvm::BasicBlock const *Src) const {
    return Sources.front() == Src;
  }

  unsigned getDepth() const { return Sources.size(); };
  std::vector<std::string> const &getNames() const { return Names; }

  void push(llvm::StringRef Collective, llvm::BasicBlock const *Source,
            bool ForcePush = false);
  void push(CollList const *CL, llvm::BasicBlock const *Source,
            bool ForcePush = false);

  std::string toCollMap() const;
#ifndef NDEBUG
  std::string toString() const;
#endif

  bool operator<(CollList const &O) const {
    std::string CollMap = toCollMap();
    std::string OCollMap = O.toCollMap();
    unsigned D1 = getDepth();
    unsigned D2 = O.getDepth();
    return std::tie(D1, Navs, CollMap) < std::tie(D2, O.Navs, OCollMap);
    // return std::tie(navs, CollMap) < std::tie(O.navs, OCollMap);
  }

  bool operator==(CollList const &O) const {
    return !(*this < O) && !(O < *this);
  }
};

#endif /* COLLLIST_H */
