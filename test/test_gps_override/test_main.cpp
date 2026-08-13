#include <gtest/gtest.h>
#include <helpers/GpsOverride.h>

static constexpr uint32_t HOUR_MS = 60U * 60U * 1000U;

TEST(GpsOverride, TimedModeExpires) {
  mesh::GpsOverride override;
  override.startTimed(1000U, 6U * HOUR_MS);
  EXPECT_TRUE(override.active(1000U));
  EXPECT_EQ(override.remainingSeconds(1000U), 21600U);
  EXPECT_FALSE(override.expireIfNeeded(1000U + 6U * HOUR_MS - 1U));
  EXPECT_TRUE(override.active(1000U + 6U * HOUR_MS - 1U));
  EXPECT_TRUE(override.expireIfNeeded(1000U + 6U * HOUR_MS));
  EXPECT_FALSE(override.active(1000U + 6U * HOUR_MS));
}

TEST(GpsOverride, ContinuousModeNeedsExplicitClear) {
  mesh::GpsOverride override;
  override.startContinuous(5000U);
  EXPECT_TRUE(override.active(5000U + 30U * HOUR_MS));
  EXPECT_FALSE(override.expireIfNeeded(5000U + 30U * HOUR_MS));
  override.clear();
  EXPECT_FALSE(override.active(5000U + 30U * HOUR_MS));
}

TEST(GpsOverride, TimedModeSurvivesMillisWrap) {
  mesh::GpsOverride override;
  const uint32_t start = 0xFFF00000U;
  override.startTimed(start, 2U * HOUR_MS);
  EXPECT_TRUE(override.active(start + HOUR_MS));
  EXPECT_TRUE(override.expireIfNeeded(start + 2U * HOUR_MS));
}

TEST(GpsOverride, ReplacementRestartsTimer) {
  mesh::GpsOverride override;
  override.startTimed(0U, 6U * HOUR_MS);
  override.startTimed(5U * HOUR_MS, 24U * HOUR_MS);
  EXPECT_EQ(override.remainingSeconds(5U * HOUR_MS), 86400U);
  EXPECT_TRUE(override.active(28U * HOUR_MS));
  EXPECT_TRUE(override.expireIfNeeded(29U * HOUR_MS));
}
