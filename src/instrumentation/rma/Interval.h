#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
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
  static constexpr size_t FILENAME_MAX_LENGTH = 128;
  uint64_t Line;
  // NOTE: we can't use a std::string here because this gets MPI sent.
  char Filename[FILENAME_MAX_LENGTH];
  DebugInfo() = default;
  DebugInfo(uint64_t Line_, char const *Filename_) : Line(Line_), Filename{} {
    if (Filename_) {
      strncpy(Filename, Filename_, FILENAME_MAX_LENGTH - 1);
      Filename[FILENAME_MAX_LENGTH - 1] = '\0';
    }
  }

  // We explicitly implement this copy constructor because we want to copy
  // the Filename on copy, and not just the pointer.
  DebugInfo(DebugInfo const &Other) : DebugInfo(Other.Line, Other.Filename) {}
};

struct Access {
  static constexpr size_t FILENAME_MAX_LENGTH = 128;
  Interval Itv;
  AccessType Type;
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

  friend inline bool operator<(std::reference_wrapper<Access const> A,
                               std::reference_wrapper<Access const> B) {
    return A.get() < B.get();
  }
  inline bool operator<(Access const &Other) const { return Itv < Other.Itv; }
};

std::ostream &operator<<(std::ostream &Os, AccessType const &T);
std::ostream &operator<<(std::ostream &Os, Interval const &I);
std::ostream &operator<<(std::ostream &Os, Access const &A);
std::ostream &operator<<(std::ostream &Os, DebugInfo const &Dbg);

} // namespace parcoach::rma
