#include "Interval.h"

#include <cstring>
#include <tuple>

namespace parcoach::rma {

std::ostream &operator<<(std::ostream &Os, AccessType const &T) {
  Os << to_string(T);
  return Os;
}

std::ostream &operator<<(std::ostream &Os, Interval const &I) {
  Os << "[" << I.Low << "," << I.Up << "]";
  return Os;
}

std::ostream &operator<<(std::ostream &Os, Access const &A) {
  Os << "[" << A.Itv << "," << A.Type << "]";
  return Os;
}

std::ostream &operator<<(std::ostream &Os, DebugInfo const &Dbg) {
  Os << Dbg.Filename << ":" << Dbg.Line;
  return Os;
}

} // namespace parcoach::rma
