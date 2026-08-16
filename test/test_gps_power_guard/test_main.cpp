#include <gtest/gtest.h>

#include <helpers/GpsPowerGuard.h>

TEST(GpsPowerGuard, ParsesAndFormatsEveryMode) {
  mesh::GpsPowerGuardMode mode = mesh::GpsPowerGuardMode::ECONOMY;
  EXPECT_TRUE(mesh::GpsPowerGuard::parseMode("critical", mode));
  EXPECT_EQ(mode, mesh::GpsPowerGuardMode::CRITICAL);
  EXPECT_STREQ(mesh::GpsPowerGuard::modeName(mode), "critical");
  EXPECT_TRUE(mesh::GpsPowerGuard::parseMode("off", mode));
  EXPECT_EQ(mode, mesh::GpsPowerGuardMode::OFF);
  EXPECT_FALSE(mesh::GpsPowerGuard::parseMode("unsafe", mode));
}

TEST(GpsPowerGuard, EconomyModeStopsGpsEarly) {
  using mesh::GpsPowerGuard;
  using mesh::GpsPowerGuardMode;
  EXPECT_FALSE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::ECONOMY,
                                             "normal"));
  EXPECT_TRUE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::ECONOMY,
                                            "economy"));
  EXPECT_TRUE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::ECONOMY,
                                            "critical"));
}

TEST(GpsPowerGuard, CriticalModeAllowsGpsInEconomy) {
  using mesh::GpsPowerGuard;
  using mesh::GpsPowerGuardMode;
  EXPECT_FALSE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::CRITICAL,
                                             "economy"));
  EXPECT_TRUE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::CRITICAL,
                                            "critical"));
}

TEST(GpsPowerGuard, OffModeStillStopsAtFinalSystemOff) {
  using mesh::GpsPowerGuard;
  using mesh::GpsPowerGuardMode;
  EXPECT_FALSE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::OFF,
                                             "economy"));
  EXPECT_FALSE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::OFF,
                                             "critical"));
  EXPECT_TRUE(GpsPowerGuard::shouldSuppress(GpsPowerGuardMode::OFF,
                                            "systemoff"));
}
