#ifndef COLLLIST_H
#define COLLLIST_H

#include <set>
#include <vector>

#include "llvm/IR/Module.h"

class CollList {
private:
  // Set of CollList head
  static std::set<CollList *> listsHeads;
  static int nb_alloc;

  unsigned rc = 0; // Ref Counter

  inline void incRef() { this->rc++; }
  inline void decRef() { this->rc--; }

protected:
  bool navs = false;
  unsigned depth = 0;

  const llvm::BasicBlock *source;
  std::vector<std::string> names;
  CollList *next = nullptr;

public:
  CollList() {
    // nb_alloc += 1;
    rc = 0;
    depth = 0;
    next = nullptr;
  }

  // Construct new list node
  CollList(std::string coll, CollList *from, const llvm::BasicBlock *src)
      : source(src), next(from) {
    // nb_alloc += 1;
    rc = 0;
    navs = (coll == "NAVS");
    names.push_back(coll);
    if (next) {
      from->incRef();
      navs |= from->isNAVS();
      depth = from->getDepth() + 1;
      // Update list head
      if (listsHeads.find(from) != listsHeads.end())
        listsHeads.erase(from);
      listsHeads.insert(this);
    } else {
      next = new CollList();
    }
  }

  CollList(CollList *coll, CollList *from, const llvm::BasicBlock *src)
      : source(src), next(from) {
    // nb_alloc += 1;
    rc = 0;
    if (next) {
      from->incRef();
      navs |= from->isNAVS();
      depth = from->getDepth() + 1;
      // Update list head
      if (listsHeads.find(from) != listsHeads.end())
        listsHeads.erase(from);
      listsHeads.insert(this);
    } else {
      next = new CollList();
    }

    this->pushColl(coll);
  }

  ~CollList() {
    // nb_alloc--;
    if (next) {
      next->decRef();
      if (next->rc == 0) {
        delete next;
      }
    }
  }

  static void freeAll() {
    for (auto &lh : listsHeads)
      delete lh;
  }

  bool isNAVS() const;
  bool isEmpty() const;
  bool isSource(const llvm::BasicBlock *src) const;

  unsigned getDepth() const;
  CollList *getNext() const;
  const std::vector<std::string> getNames() const;

  void pushColl(std::string coll);
  void pushColl(CollList *l);

  std::string toString();
  std::string toCollMap() const;

  bool operator==(const CollList &cl2) {
    const CollList &cl1 = *this;

    if (cl1.depth != cl2.depth)
      return false;

    if (cl1.navs != cl2.navs)
      return false;

    std::string t = cl1.toCollMap();
    std::string o = cl2.toCollMap();
    return (t == o);
  }
};

#endif /* COLLLIST_H */
