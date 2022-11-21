#ifndef COLLLIST_H
#define COLLLIST_H

#include <set>
#include <vector>

#include "llvm/IR/Module.h"

// FIXME: leaks -> unique_ptr somewhere
class CollList {
  bool navs;

  llvm::SmallVector<llvm::BasicBlock const *, 4> Sources;
  std::vector<std::string> names;

public:
  // Construct new list node
  CollList(llvm::StringRef coll, const llvm::BasicBlock *src);
  CollList(CollList const *coll, const llvm::BasicBlock *src);
  CollList() = default;
  CollList(CollList const &) = default;
  ~CollList() = default;

  bool isNAVS() const { return navs; }
  bool isSource(const llvm::BasicBlock *src) const {
    return Sources.front() == src;
  }

  unsigned getDepth() const { return Sources.size(); };
  const std::vector<std::string> &getNames() const { return names; }

  void push(llvm::StringRef Collective, llvm::BasicBlock const *Source,
            bool ForcePush = false);
  void push(CollList const *CL, llvm::BasicBlock const *Source,
            bool ForcePush = false);

  std::string toCollMap() const;
#ifndef NDEBUG
  std::string toString() const;
#endif

  bool operator<(const CollList &O) const {
    std::string CollMap = toCollMap();
    std::string OCollMap = O.toCollMap();
    unsigned d1 = getDepth();
    unsigned d2 = O.getDepth();
    return std::tie(d1, navs, CollMap) < std::tie(d2, O.navs, OCollMap);
    // return std::tie(navs, CollMap) < std::tie(O.navs, OCollMap);
  }

  bool operator==(const CollList &O) const {
    return !(*this < O) && !(O < *this);
  }
};

#endif /* COLLLIST_H */
