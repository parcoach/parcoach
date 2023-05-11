#pragma once

#include "parcoach/Collectives.h"

#include "llvm/ADT/DenseMap.h"

#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace llvm {
class BasicBlock;
class Value;
} // namespace llvm
class PTACallGraph;
namespace parcoach {

struct Element {
  virtual ~Element() = default;
  virtual std::string toString() const = 0;
  virtual std::unique_ptr<Element> copy() const = 0;
  virtual bool navs() const = 0;
};

struct CollElement : Element {
  CollElement(Collective const &C) : Value(C) {}
  ~CollElement() = default;
  Collective const &Value;
  std::string toString() const override { return Value.Name; }
  bool navs() const override { return false; }
  inline std::unique_ptr<Element> copy() const override {
    return std::make_unique<CollElement>(Value);
  }
};

// Not a valid set.
struct NavsElement : Element {
  ~NavsElement() = default;
  std::string toString() const override { return "NAVS"; }
  // Maybe this method can be an isa
  bool navs() const override { return true; }
  inline std::unique_ptr<Element> copy() const override {
    return std::make_unique<NavsElement>();
  }
};

class CollectiveList {
  // An optional LoopHeader for the loop this may represent.
  // Used with two objectives:
  //   - be able to identify if the CL belongs to a list
  //   - be able to emit two different strings for two different loops
  //   even if they have the same list of collectives. We must do that because
  //   we don't know the boundaries.
  std::optional<llvm::BasicBlock *> LoopHeader_;
  using ListTy = std::deque<std::unique_ptr<Element>>;
  ListTy List_;
  static void add(CollectiveList const &Other,
                  std::insert_iterator<ListTy> Inserter);

public:
  using BBToCollListMap = llvm::DenseMap<llvm::BasicBlock *, CollectiveList>;
  using CommToBBToCollListMap = llvm::DenseMap<llvm::Value *, BBToCollListMap>;
  std::string toString() const;
  CollectiveList() = default;
  CollectiveList(CollectiveList &&From) = default;
  CollectiveList &operator=(CollectiveList &&From) = default;
  // This forces move-assignment, or explicit copy.
  CollectiveList &operator=(CollectiveList const &From) = delete;
  CollectiveList(CollectiveList const &From);
  CollectiveList(llvm::BasicBlock *Header, CollectiveList const &LoopList);
  bool navs() const;
  bool empty() const { return List_.empty(); }
  auto const &getLoopHeader() const { return LoopHeader_; }
  // Poperly "push" a CL to our CL.
  void extendWith(CollectiveList const &Other);
  void prependWith(CollectiveList const &Other);

  template <typename ElementTy, typename... ArgsTy>
  void extendWith(ArgsTy &&...Args) {
    List_.emplace_back(std::make_unique<ElementTy>(Args...));
  }

  // This is likely too expensive.
  inline bool operator==(CollectiveList const &Other) const {
    return toString() == Other.toString();
  }
  inline bool operator!=(CollectiveList const &Other) const {
    return !operator==(Other);
  }

  template <typename ContainerTy, typename IteratorTy>
  static bool NeighborsAreNAVS(ContainerTy &Computed, llvm::BasicBlock *BB,
                               IteratorTy NeighborsBegin,
                               IteratorTy NeighborsEnd) {
    // If at least one pair of neighbors are not equal, then the result
    // is NAVS. We leverage std::adjacent_find to figure this out.
    auto NotEqual = [&](llvm::BasicBlock *A, llvm::BasicBlock *B) {
      assert(Computed.count(A) && Computed.count(B) &&
             "Both BB should be computed already");
      return Computed[A] != Computed[B];
    };
    return std::adjacent_find(NeighborsBegin, NeighborsEnd, NotEqual) !=
           NeighborsEnd;
  }

  static CollectiveList CreateFromBB(BBToCollListMap const &CollLists,
                                     PTACallGraph const &PTACG,
                                     bool NeighborsAreNAVS,
                                     llvm::BasicBlock &BB,
                                     llvm::Value *Comm = nullptr);
};

struct CollListElement : Element {
  CollectiveList Value;
  CollListElement(CollectiveList Val) : Value(std::move(Val)) {}
  ~CollListElement() = default;
  std::string toString() const override { return Value.toString(); }
  bool navs() const override { return Value.navs(); }
  inline std::unique_ptr<Element> copy() const override {
    return std::make_unique<CollListElement>(Value);
  }
};

} // namespace parcoach
