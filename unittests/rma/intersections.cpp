#include "rma_analyzer.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace parcoach::rma;

namespace {

TEST(RMAIntervals, FindIntersections) {
  using ::testing::ElementsAre;
  rma_analyzer_state State;
  auto &Intervals = State.Intervals;
  Access A{Interval{10, 12}, AccessType::RMA_READ, DebugInfo()};
  Access B{Interval{6, 8}, AccessType::RMA_READ, DebugInfo()};
  Access C{Interval{0, 4}, AccessType::RMA_WRITE, DebugInfo()};
  Intervals.insert(A);
  Intervals.insert(B);
  Intervals.insert(C);

  Access NewAccess{Interval{1, 7}, AccessType::RMA_READ, DebugInfo()};
  auto Intersecting = State.getIntersectingIntervals(NewAccess);
  EXPECT_THAT(Intersecting, ElementsAre(C, B));

  auto Conflicting = State.getConflictingIntervals(NewAccess);
  EXPECT_THAT(Conflicting, ElementsAre(C));
}

} // namespace
