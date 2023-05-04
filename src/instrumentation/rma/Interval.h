#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <tuple>

namespace parcoach::rma {

enum class AccessType {
  LOCAL_READ = 0,
  LOCAL_WRITE = 1,
  RMA_READ = 2,
  RMA_WRITE = 3,
};

constexpr char const *to_string(AccessType T) {
  switch (T) {
  case AccessType::LOCAL_READ:
    return "LOCAL_READ";
  case AccessType::LOCAL_WRITE:
    return "LOCAL_WRITE";
  case AccessType::RMA_READ:
    return "RMA_READ";
  case AccessType::RMA_WRITE:
    return "RMA_WRITE";
  default:
    return "UNKNOWN";
  }
}

struct Interval {
  uint64_t Low;
  uint64_t Up;
  inline void fixForWindow(uint64_t BaseAddr) {
    Low += BaseAddr;
    Up += BaseAddr;
  }
  inline bool operator<(Interval const &Other) const {
    return std::tie(Low, Up) < std::tie(Other.Low, Other.Up);
  }
  inline bool intersects(Interval const &Other) const {
    return !(Low > Other.Up || Up < Other.Low);
  }
};

struct MemoryAccess {
  uint64_t Addr;
  uint64_t Size;
};

struct DebugInfo {
  uint64_t Line{};
  // It's absolutely essential that this field is the *last* member of that
  // struct. It gets sent separately through MPI, and we don't include it
  // in the MPI committed type.
  std::string Filename{"unknown_file"};
  DebugInfo() = default;
  DebugInfo(int Line_, char const *Filename_)
      : Line(static_cast<uint64_t>(Line_)) {
    // Only construct the filename if it's actually containing something.
    if (Filename_) {
      Filename = Filename_;
    }
  }
};

struct Access {
  Interval Itv;
  AccessType Type;
  // It's absolutely essential that this field is the *last* member of that
  // struct.
  DebugInfo Dbg;

  Access() = default;

  Access(Interval Itv_, AccessType Type_, DebugInfo Dbg_)
      : Itv(std::move(Itv_)), Type{Type_}, Dbg(std::move(Dbg_)) {}

  Access(MemoryAccess Acc, AccessType T, DebugInfo Dbg)
      : Access(Interval{Acc.Addr, Acc.Addr + Acc.Size - 1}, T, std::move(Dbg)) {
  }

  // Return true if this intersects with Other and they have a conflicting
  // access type.
  inline bool conflictsWith(Access const &Other) const {
    if (!Itv.intersects(Other.Itv)) {
      return false;
    }
    /* By doing a bitwise OR on the two access types, the resulting
     * value is an error only if the two bits are at 1 (i.e. there is at
     * least a local WRITE access combined with a remote access, or a
     * remote WRITE access). */
    return ((int)Type | (int)Other.Type) == (int)AccessType::RMA_WRITE;
  }
};

// This is a comparator specifically designed for our multiset: we just
// want to compare the Intervals' ranges.
struct AccessComp {
  inline bool operator()(Access const &lhs, Access const &rhs) const {
    return lhs.Itv < rhs.Itv;
  }
};

using IntervalContainer = std::multiset<Access, AccessComp>;
using IntervalViewContainer =
    std::multiset<std::reference_wrapper<Access const>, AccessComp>;

std::ostream &operator<<(std::ostream &Os, AccessType const &T);
std::ostream &operator<<(std::ostream &Os, Interval const &I);
std::ostream &operator<<(std::ostream &Os, Access const &A);
std::ostream &operator<<(std::ostream &Os, DebugInfo const &Dbg);

} // namespace parcoach::rma
