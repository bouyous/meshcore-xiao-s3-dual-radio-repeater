#include <gtest/gtest.h>
#include <helpers/OperationalEventRecord.h>

TEST(OperationalEventRecord, StablePackedLayoutAndValidCrc) {
  mesh::OperationalEventRecord record = {};
  record.event = (uint8_t)mesh::OperationalEventCode::GPS_FIRST_FIX;
  record.sequence = 42;
  record.boot_id = 7;
  record.flags = mesh::OP_EVENT_TIME_TRUSTED;
  record.epoch = 1786550400U;
  record.uptime_seconds = 123;
  record.value_a = 47;
  record.value_b = 9;
  mesh::sealOperationalEvent(record);

  EXPECT_EQ(sizeof(record), 30U);
  EXPECT_TRUE(mesh::isOperationalEventValid(record));
}

TEST(OperationalEventRecord, DetectsInterruptedOrCorruptWrite) {
  mesh::OperationalEventRecord record = {};
  record.event = (uint8_t)mesh::OperationalEventCode::POWER_SHUTDOWN;
  record.sequence = 5;
  record.value_a = 3290;
  mesh::sealOperationalEvent(record);
  ASSERT_TRUE(mesh::isOperationalEventValid(record));

  record.value_a ^= 1;
  EXPECT_FALSE(mesh::isOperationalEventValid(record));
}

TEST(OperationalEventRecord, SequenceComparisonSurvivesWrap) {
  EXPECT_TRUE(mesh::operationalSequenceAfter(1U, 0xFFFFFFFEU));
  EXPECT_FALSE(mesh::operationalSequenceAfter(0xFFFFFFFEU, 1U));
}

TEST(OperationalEventRecord, EstimatesBootTimeFromLaterTrustedGpsAnchor) {
  mesh::OperationalEventRecord boot = {};
  boot.boot_id = 4;
  boot.uptime_seconds = 2;

  mesh::OperationalEventRecord fix = {};
  fix.boot_id = 4;
  fix.flags = mesh::OP_EVENT_TIME_TRUSTED;
  fix.uptime_seconds = 122;
  fix.epoch = 1786550520U;

  uint32_t estimated = 0;
  EXPECT_TRUE(mesh::estimateOperationalEventEpoch(boot, fix, estimated));
  EXPECT_EQ(estimated, 1786550400U);
}

TEST(OperationalEventRecord, RefusesEstimateAcrossBootsOrBackwardsUptime) {
  mesh::OperationalEventRecord event = {};
  event.boot_id = 3;
  event.uptime_seconds = 200;

  mesh::OperationalEventRecord anchor = {};
  anchor.boot_id = 4;
  anchor.flags = mesh::OP_EVENT_TIME_TRUSTED;
  anchor.uptime_seconds = 100;
  anchor.epoch = 1786550500U;

  uint32_t estimated = 0;
  EXPECT_FALSE(mesh::estimateOperationalEventEpoch(event, anchor, estimated));
  anchor.boot_id = 3;
  EXPECT_FALSE(mesh::estimateOperationalEventEpoch(event, anchor, estimated));
}

TEST(OperationalEventRecord, RejectsFallbackGpsDates) {
  EXPECT_FALSE(mesh::operationalEpochPlausible(946684800U));   // 2000-01-01
  EXPECT_FALSE(mesh::operationalEpochPlausible(1715770351U));  // firmware fallback
  EXPECT_TRUE(mesh::operationalEpochPlausible(1786550400U));   // 2026-08-12
}
