#include <gtest/gtest.h>
#include <helpers/GpsSchedule.h>

static constexpr uint32_t HOUR_MS = 60U * 60U * 1000U;
static constexpr uint32_t DAY_MS = 24U * HOUR_MS;

TEST(GpsSchedule, RunsForFirstHourThenOncePerDay) {
  mesh::GpsSchedule schedule(DAY_MS, HOUR_MS);
  schedule.setEnabled(true, 1000U);

  EXPECT_TRUE(schedule.shouldRun(1000U));
  EXPECT_TRUE(schedule.shouldRun(1000U + HOUR_MS - 1U));
  EXPECT_FALSE(schedule.shouldRun(1000U + HOUR_MS));
  EXPECT_TRUE(schedule.shouldRun(1000U + DAY_MS));
}

TEST(GpsSchedule, PowerSavingSuppressesWithoutMovingWindow) {
  mesh::GpsSchedule schedule(DAY_MS, HOUR_MS);
  schedule.setEnabled(true, 0U);
  schedule.setSuppressed(true);
  EXPECT_TRUE(schedule.windowOpen(30U * 60U * 1000U));
  EXPECT_FALSE(schedule.shouldRun(30U * 60U * 1000U));

  schedule.setSuppressed(false);
  EXPECT_TRUE(schedule.shouldRun(30U * 60U * 1000U));
  EXPECT_FALSE(schedule.shouldRun(HOUR_MS));
}

TEST(GpsSchedule, ReportsRemainingAndNextWindow) {
  mesh::GpsSchedule schedule(DAY_MS, HOUR_MS);
  schedule.setEnabled(true, 0U);
  EXPECT_EQ(schedule.secondsRemaining(30U * 60U * 1000U), 1800U);
  EXPECT_EQ(schedule.secondsUntilNextWindow(2U * HOUR_MS), 22U * 60U * 60U);
}

TEST(GpsSchedule, DisableAndReenableStartsFreshWindow) {
  mesh::GpsSchedule schedule(DAY_MS, HOUR_MS);
  schedule.setEnabled(true, 0U);
  schedule.setEnabled(false, 1000U);
  EXPECT_FALSE(schedule.shouldRun(1000U));
  schedule.setEnabled(true, 5000U);
  EXPECT_TRUE(schedule.shouldRun(5000U));
  EXPECT_FALSE(schedule.shouldRun(5000U + HOUR_MS));
}

TEST(GpsSchedule, MillisWrapIsSafeAcrossActiveWindow) {
  mesh::GpsSchedule schedule(DAY_MS, HOUR_MS);
  const uint32_t start = 0xFFF00000U;
  schedule.setEnabled(true, start);
  EXPECT_TRUE(schedule.shouldRun(start + 30U * 60U * 1000U));
  EXPECT_FALSE(schedule.shouldRun(start + HOUR_MS));
}

TEST(GpsSchedule, WindowCannotExceedPeriod) {
  mesh::GpsSchedule schedule(HOUR_MS, DAY_MS);
  schedule.setEnabled(true, 0U);
  EXPECT_TRUE(schedule.shouldRun(HOUR_MS - 1U));
}
