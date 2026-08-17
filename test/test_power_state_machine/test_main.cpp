#include <gtest/gtest.h>
#include <helpers/PowerStateMachine.h>

static const mesh::PowerStateConfig config = {
  3500, 3300, 3350, 50, 600000, 1000, 5000
};

TEST(PowerStateMachine, NormalEconomyHysteresis) {
  mesh::PowerStateMachine power(config);
  EXPECT_EQ(power.update(0, 3700, false), mesh::PowerAction::NONE);
  EXPECT_EQ(power.state(), mesh::PowerState::NORMAL);
  power.update(30000, 3490, false);
  EXPECT_EQ(power.state(), mesh::PowerState::ECONOMY);
  power.update(60000, 3520, false);
  EXPECT_EQ(power.state(), mesh::PowerState::ECONOMY);
  power.update(90000, 3550, false);
  EXPECT_EQ(power.state(), mesh::PowerState::NORMAL);
}

TEST(PowerStateMachine, ShortDipDoesNotShutdown) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3290, false);
  EXPECT_EQ(power.state(), mesh::PowerState::CRITICAL);
  EXPECT_EQ(power.update(599999, 3290, false), mesh::PowerAction::NONE);
  power.update(600000, 3350, false);
  EXPECT_EQ(power.state(), mesh::PowerState::ECONOMY);
  EXPECT_EQ(power.criticalDurationSeconds(600000), 0U);
}

TEST(PowerStateMachine, ContinuousCriticalVoltageShutsDown) {
  mesh::PowerStateMachine power(config);
  power.update(1000, 3290, false);
  EXPECT_EQ(power.update(600999, 3290, false), mesh::PowerAction::NONE);
  EXPECT_EQ(power.update(601000, 3290, false), mesh::PowerAction::SHUTDOWN);
  EXPECT_EQ(power.state(), mesh::PowerState::SYSTEM_OFF);
}

TEST(PowerStateMachine, CriticalHysteresisPreventsTimerChatter) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3290, false);
  power.update(300000, 3320, false);
  EXPECT_EQ(power.state(), mesh::PowerState::CRITICAL);
  EXPECT_EQ(power.update(600000, 3320, false), mesh::PowerAction::SHUTDOWN);
}

TEST(PowerStateMachine, BoundariesMatchDocumentedPolicy) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3500, false);
  EXPECT_EQ(power.state(), mesh::PowerState::NORMAL);
  power.update(30000, 3300, false);
  EXPECT_EQ(power.state(), mesh::PowerState::ECONOMY);
  EXPECT_EQ(power.criticalDurationSeconds(30000), 0U);
  power.update(60000, 3299, false);
  EXPECT_EQ(power.state(), mesh::PowerState::CRITICAL);
  power.update(90000, 3350, false);
  EXPECT_EQ(power.state(), mesh::PowerState::ECONOMY);
  EXPECT_EQ(power.criticalDurationSeconds(90000), 0U);
}

TEST(PowerStateMachine, ThirtySecondSamplingRequiresFullTenMinutes) {
  mesh::PowerStateMachine power(config);
  for (uint32_t second = 0; second < 600; second += 30) {
    EXPECT_EQ(power.update(second * 1000U, 3290, false), mesh::PowerAction::NONE);
  }
  EXPECT_EQ(power.update(600000, 3290, false), mesh::PowerAction::SHUTDOWN);
}

TEST(PowerStateMachine, ExtremelyLowBatteryIsCriticalNotInvalid) {
  mesh::PowerStateMachine power(config);
  power.update(0, 2400, false);
  EXPECT_EQ(power.state(), mesh::PowerState::CRITICAL);
  EXPECT_EQ(power.update(600000, 2400, false), mesh::PowerAction::SHUTDOWN);
}

TEST(PowerStateMachine, InvalidAdcAndExternalPowerCannotShutdown) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3290, false);
  power.update(600000, 0, false);
  EXPECT_EQ(power.state(), mesh::PowerState::CRITICAL);
  EXPECT_EQ(power.update(600001, 3290, true), mesh::PowerAction::NONE);
  EXPECT_EQ(power.state(), mesh::PowerState::NORMAL);
  EXPECT_EQ(power.criticalDurationSeconds(900000), 0U);
}

TEST(PowerStateMachine, InvalidReadingBreaksContinuousCriticalProof) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3290, false);
  power.update(600000, 0, false);
  EXPECT_EQ(power.update(600001, 3290, false), mesh::PowerAction::NONE);
  EXPECT_EQ(power.criticalDurationSeconds(600001), 0U);
  EXPECT_EQ(power.update(1200001, 3290, false), mesh::PowerAction::SHUTDOWN);
}

TEST(PowerStateMachine, UsbDoesNotPublishInvalidVoltage) {
  mesh::PowerStateMachine power(config);
  power.update(0, 3700, false);
  power.update(1000, 0, true);
  EXPECT_EQ(power.lastValidMilliVolts(), 3700);
  EXPECT_EQ(power.state(), mesh::PowerState::NORMAL);
}

TEST(PowerStateMachine, MillisWrapIsSafe) {
  mesh::PowerStateMachine power(config);
  const uint32_t start = 0xFFF70000U;
  power.update(start, 3290, false);
  EXPECT_EQ(power.update(start + 600000U, 3290, false), mesh::PowerAction::SHUTDOWN);
}
