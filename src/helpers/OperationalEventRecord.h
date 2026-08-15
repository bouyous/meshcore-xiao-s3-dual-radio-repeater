#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mesh {

static constexpr uint16_t OPERATIONAL_EVENT_MAGIC = 0x5031;  // "P1"
static constexpr uint8_t OPERATIONAL_EVENT_VERSION = 1;

enum class OperationalEventCode : uint8_t {
  BOOT = 1,
  BOOT_COMPLETE = 2,
  INIT_ERROR = 3,
  LOG_CLEARED = 4,

  POWER_NORMAL = 10,
  POWER_ECONOMY = 11,
  POWER_CRITICAL = 12,
  POWER_SHUTDOWN = 13,
  POWER_RECOVERY_RESET = 14,
  BUTTON_STATE = 15,

  GPS_DETECTED = 20,
  GPS_NOT_DETECTED = 21,
  GPS_WINDOW_START = 22,
  GPS_FIRST_FIX = 23,
  GPS_TIME_SYNC = 24,
  GPS_WINDOW_END = 25,
  GPS_OVERRIDE_START = 26,
  GPS_OVERRIDE_END = 27,

  RADIO_READY = 30
};

enum OperationalEventFlags : uint16_t {
  OP_EVENT_TIME_TRUSTED = 0x0001,
  OP_EVENT_ERROR = 0x0002,
  OP_EVENT_EXTERNAL_POWER = 0x0004
};

#pragma pack(push, 1)
struct OperationalEventRecord {
  uint16_t magic;
  uint8_t version;
  uint8_t event;
  uint32_t sequence;
  uint16_t boot_id;
  uint16_t flags;
  uint32_t epoch;
  uint32_t uptime_seconds;
  int32_t value_a;
  int32_t value_b;
  uint16_t crc;
};
#pragma pack(pop)

static_assert(sizeof(OperationalEventRecord) == 30,
              "OperationalEventRecord layout must stay stable");

inline uint16_t operationalEventCrc(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                            : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

inline void sealOperationalEvent(OperationalEventRecord& record) {
  record.magic = OPERATIONAL_EVENT_MAGIC;
  record.version = OPERATIONAL_EVENT_VERSION;
  record.crc = operationalEventCrc(
      reinterpret_cast<const uint8_t*>(&record),
      offsetof(OperationalEventRecord, crc));
}

inline bool isOperationalEventValid(const OperationalEventRecord& record) {
  if (record.magic != OPERATIONAL_EVENT_MAGIC ||
      record.version != OPERATIONAL_EVENT_VERSION) {
    return false;
  }
  return record.crc == operationalEventCrc(
      reinterpret_cast<const uint8_t*>(&record),
      offsetof(OperationalEventRecord, crc));
}

inline bool operationalSequenceAfter(uint32_t lhs, uint32_t rhs) {
  return (int32_t)(lhs - rhs) > 0;
}

inline bool operationalEpochPlausible(uint32_t epoch) {
  // Reject the common 2000/2024 fallback values and dates outside the useful
  // lifetime of this firmware. A navigation fix alone does not guarantee that
  // the NMEA date field was decoded correctly.
  return epoch >= 1735689600U && epoch < 4102444800U;  // 2025-01-01 .. 2100-01-01
}

inline bool estimateOperationalEventEpoch(
    const OperationalEventRecord& event,
    const OperationalEventRecord& trusted_anchor,
    uint32_t& estimated_epoch) {
  if (event.boot_id != trusted_anchor.boot_id ||
      !(trusted_anchor.flags & OP_EVENT_TIME_TRUSTED) ||
      trusted_anchor.uptime_seconds < event.uptime_seconds) {
    return false;
  }
  const uint32_t delta = trusted_anchor.uptime_seconds - event.uptime_seconds;
  if (trusted_anchor.epoch < delta) return false;
  estimated_epoch = trusted_anchor.epoch - delta;
  return true;
}

}  // namespace mesh
