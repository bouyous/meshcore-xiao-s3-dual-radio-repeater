#include "P1EventJournal.h"

#if defined(P1_EVENT_LOG)

#include <RTClib.h>
#include <helpers/NRF52Board.h>
#include <nrf.h>

using namespace Adafruit_LittleFS_Namespace;

static const char* resetReasonName(uint32_t reason) {
  if (reason & POWER_RESETREAS_RESETPIN_Msk) return "reset_pin";
  if (reason & POWER_RESETREAS_DOG_Msk) return "watchdog";
  if (reason & POWER_RESETREAS_SREQ_Msk) return "soft_reset";
  if (reason & POWER_RESETREAS_LOCKUP_Msk) return "cpu_lockup";
#ifdef POWER_RESETREAS_LPCOMP_Msk
  if (reason & POWER_RESETREAS_LPCOMP_Msk) return "lpcomp_wake";
#endif
#ifdef POWER_RESETREAS_VBUS_Msk
  if (reason & POWER_RESETREAS_VBUS_Msk) return "vbus_wake";
#endif
#ifdef POWER_RESETREAS_OFF_Msk
  if (reason & POWER_RESETREAS_OFF_Msk) return "gpio_wake";
#endif
  return "cold_boot";
}

static const char* shutdownReasonName(uint8_t reason) {
  switch (reason) {
    case SHUTDOWN_REASON_LOW_VOLTAGE: return "low_voltage";
    case SHUTDOWN_REASON_USER: return "user";
    case SHUTDOWN_REASON_BOOT_PROTECT: return "boot_protect";
    default: return "none";
  }
}

static const char* initErrorName(int32_t error) {
  switch (error) {
    case P1EventJournal::INIT_ERROR_RADIO: return "radio_init";
    case P1EventJournal::INIT_ERROR_SENSORS: return "sensors_init";
    default: return "unknown_init";
  }
}

uint8_t P1EventJournal::loadRecords(mesh::OperationalEventRecord* records,
                                    uint8_t capacity) const {
  if (!fs || !records || capacity == 0 || !fs->exists(FILE_PATH)) return 0;

  File file = fs->open(FILE_PATH, FILE_O_READ);
  if (!file) return 0;

  const uint32_t slots = min((uint32_t)MAX_RECORDS,
      file.size() / (uint32_t)sizeof(mesh::OperationalEventRecord));
  uint8_t count = 0;
  for (uint32_t slot = 0; slot < slots && count < capacity; slot++) {
    mesh::OperationalEventRecord record = {};
    if (!file.seek(slot * sizeof(record)) ||
        file.read(&record, sizeof(record)) != (int)sizeof(record)) {
      continue;
    }
    if (mesh::isOperationalEventValid(record)) records[count++] = record;
  }
  file.close();
  return count;
}

bool P1EventJournal::begin(Adafruit_LittleFS& filesystem,
                           mesh::RTCClock& clock) {
  fs = &filesystem;
  rtc = &clock;
  ready = true;
  time_trusted = false;
  write_errors = 0;
  next_sequence = 1;
  boot_id = 1;
  next_slot = 0;
  entry_count = 0;

  mesh::OperationalEventRecord records[MAX_RECORDS];
  const uint8_t count = loadRecords(records, MAX_RECORDS);
  entry_count = count;
  if (count == 0) return true;

  uint8_t newest = 0;
  for (uint8_t i = 1; i < count; i++) {
    if (mesh::operationalSequenceAfter(records[i].sequence,
                                       records[newest].sequence)) {
      newest = i;
    }
  }

  next_sequence = records[newest].sequence + 1U;
  boot_id = (uint16_t)(records[newest].boot_id + 1U);

  File file = fs->open(FILE_PATH, FILE_O_READ);
  if (file) {
    const uint32_t slots = min((uint32_t)MAX_RECORDS,
        file.size() / (uint32_t)sizeof(mesh::OperationalEventRecord));
    for (uint32_t slot = 0; slot < slots; slot++) {
      mesh::OperationalEventRecord record = {};
      if (file.seek(slot * sizeof(record)) &&
          file.read(&record, sizeof(record)) == (int)sizeof(record) &&
          mesh::isOperationalEventValid(record) &&
          record.sequence == records[newest].sequence) {
        next_slot = (uint8_t)((slot + 1U) % MAX_RECORDS);
        break;
      }
    }
    file.close();
  }
  return true;
}

bool P1EventJournal::recordInternal(mesh::OperationalEventCode code,
                                    int32_t value_a, int32_t value_b,
                                    uint16_t flags, uint32_t epoch,
                                    bool trusted_time) {
  if (!ready || !fs || !rtc) return false;

  mesh::OperationalEventRecord record = {};
  record.event = (uint8_t)code;
  record.sequence = next_sequence;
  record.boot_id = boot_id;
  record.flags = flags | (trusted_time ? mesh::OP_EVENT_TIME_TRUSTED : 0U);
  record.epoch = epoch;
  record.uptime_seconds = millis() / 1000U;
  record.value_a = value_a;
  record.value_b = value_b;
  mesh::sealOperationalEvent(record);

  File file = fs->open(FILE_PATH, FILE_O_WRITE);
  const uint32_t offset = (uint32_t)next_slot * sizeof(record);
  if (!file || !file.seek(offset) ||
      file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) !=
          sizeof(record)) {
    if (file) file.close();
    write_errors++;
    return false;
  }
  file.flush();
  file.close();

  mesh::OperationalEventRecord verify = {};
  File check = fs->open(FILE_PATH, FILE_O_READ);
  const bool verified = check && check.seek(offset) &&
      check.read(&verify, sizeof(verify)) == (int)sizeof(verify) &&
      mesh::isOperationalEventValid(verify) &&
      verify.sequence == record.sequence;
  if (check) check.close();
  if (!verified) {
    write_errors++;
    return false;
  }

  next_sequence++;
  next_slot = (uint8_t)((next_slot + 1U) % MAX_RECORDS);
  if (entry_count < MAX_RECORDS) entry_count++;
  return true;
}

bool P1EventJournal::record(mesh::OperationalEventCode code, int32_t value_a,
                            int32_t value_b, uint16_t flags) {
  return recordInternal(code, value_a, value_b, flags,
                        rtc ? rtc->getCurrentTime() : 0, time_trusted);
}

bool P1EventJournal::recordTrusted(mesh::OperationalEventCode code,
                                   uint32_t epoch, int32_t value_a,
                                   int32_t value_b, uint16_t flags) {
  return recordInternal(code, value_a, value_b, flags, epoch, true);
}

void P1EventJournal::recordBoot(uint32_t reset_reason,
                                uint8_t previous_shutdown_reason,
                                uint16_t boot_voltage_mv,
                                bool external_power) {
  const int32_t packed = ((int32_t)previous_shutdown_reason << 16) |
                         boot_voltage_mv;
  uint16_t flags = external_power ? mesh::OP_EVENT_EXTERNAL_POWER : 0;
  if ((reset_reason & POWER_RESETREAS_DOG_Msk) ||
      (reset_reason & POWER_RESETREAS_LOCKUP_Msk)) {
    flags |= mesh::OP_EVENT_ERROR;
  }
  record(mesh::OperationalEventCode::BOOT, (int32_t)reset_reason, packed, flags);
}

void P1EventJournal::recordBootComplete(uint16_t battery_mv) {
  record(mesh::OperationalEventCode::BOOT_COMPLETE, battery_mv, 0);
}

void P1EventJournal::recordInitError(InitError error) {
  record(mesh::OperationalEventCode::INIT_ERROR, (int32_t)error, 0,
         mesh::OP_EVENT_ERROR);
}

void P1EventJournal::recordPowerState(mesh::PowerState state,
                                      uint16_t battery_mv,
                                      uint32_t critical_seconds) {
  mesh::OperationalEventCode code;
  switch (state) {
    case mesh::PowerState::NORMAL:
      code = mesh::OperationalEventCode::POWER_NORMAL;
      break;
    case mesh::PowerState::ECONOMY:
      code = mesh::OperationalEventCode::POWER_ECONOMY;
      break;
    case mesh::PowerState::CRITICAL:
      code = mesh::OperationalEventCode::POWER_CRITICAL;
      break;
    default:
      return;
  }
  record(code, battery_mv, (int32_t)critical_seconds);
}

void P1EventJournal::recordShutdown(uint8_t reason, uint16_t battery_mv,
                                    uint32_t critical_seconds) {
  const int32_t packed = (int32_t)((critical_seconds << 8) | reason);
  record(mesh::OperationalEventCode::POWER_SHUTDOWN, battery_mv, packed);
}

void P1EventJournal::recordRecoveryReset(uint16_t battery_mv,
                                         uint32_t standby_seconds) {
  record(mesh::OperationalEventCode::POWER_RECOVERY_RESET, battery_mv,
         (int32_t)standby_seconds);
}

void P1EventJournal::onSensorRuntimeEvent(const SensorRuntimeEvent& event) {
  switch (event.type) {
    case SensorRuntimeEventType::GPS_DETECTED:
      record(mesh::OperationalEventCode::GPS_DETECTED);
      break;
    case SensorRuntimeEventType::GPS_NOT_DETECTED:
      record(mesh::OperationalEventCode::GPS_NOT_DETECTED, 0, 0,
             mesh::OP_EVENT_ERROR);
      break;
    case SensorRuntimeEventType::GPS_WINDOW_START:
      record(mesh::OperationalEventCode::GPS_WINDOW_START);
      break;
    case SensorRuntimeEventType::GPS_FIRST_FIX:
      if (mesh::operationalEpochPlausible(event.epoch)) {
        recordTrusted(mesh::OperationalEventCode::GPS_FIRST_FIX, event.epoch,
                      (int32_t)(event.duration_ms / 1000U), event.value);
      } else {
        record(mesh::OperationalEventCode::GPS_FIRST_FIX,
               (int32_t)(event.duration_ms / 1000U), event.value,
               mesh::OP_EVENT_ERROR);
      }
      break;
    case SensorRuntimeEventType::GPS_TIME_SYNC:
      if (mesh::operationalEpochPlausible(event.epoch)) {
        recordTrusted(mesh::OperationalEventCode::GPS_TIME_SYNC, event.epoch,
                      (int32_t)(event.duration_ms / 1000U), event.value);
        time_trusted = true;
      } else {
        record(mesh::OperationalEventCode::GPS_TIME_SYNC,
               (int32_t)(event.duration_ms / 1000U), event.value,
               mesh::OP_EVENT_ERROR);
      }
      break;
    case SensorRuntimeEventType::GPS_WINDOW_END:
      record(mesh::OperationalEventCode::GPS_WINDOW_END,
             (int32_t)(event.duration_ms / 1000U), event.value,
             event.value ? 0 : mesh::OP_EVENT_ERROR);
      break;
    case SensorRuntimeEventType::GPS_OVERRIDE_START:
      record(mesh::OperationalEventCode::GPS_OVERRIDE_START,
             (int32_t)(event.duration_ms / 1000U), event.value);
      break;
    case SensorRuntimeEventType::GPS_OVERRIDE_END:
      record(mesh::OperationalEventCode::GPS_OVERRIDE_END,
             (int32_t)(event.duration_ms / 1000U), event.value);
      break;
  }
}

static void printEventDetails(Print& out,
                              const mesh::OperationalEventRecord& record) {
  const auto code = (mesh::OperationalEventCode)record.event;
  switch (code) {
    case mesh::OperationalEventCode::BOOT: {
      const uint8_t shutdown_reason = (uint8_t)((uint32_t)record.value_b >> 16);
      const uint16_t voltage = (uint16_t)record.value_b;
      out.printf("BOOT reset=%s(0x%lX) previous_shutdown=%s vbat=%umV%s",
                 resetReasonName((uint32_t)record.value_a),
                 (unsigned long)(uint32_t)record.value_a,
                 shutdownReasonName(shutdown_reason), voltage,
                 (record.flags & mesh::OP_EVENT_EXTERNAL_POWER) ? " usb=yes" : "");
      break;
    }
    case mesh::OperationalEventCode::BOOT_COMPLETE:
      out.printf("BOOT_COMPLETE vbat=%ldmV", (long)record.value_a);
      break;
    case mesh::OperationalEventCode::INIT_ERROR:
      out.printf("INIT_ERROR component=%s", initErrorName(record.value_a));
      break;
    case mesh::OperationalEventCode::LOG_CLEARED:
      out.print("LOG_CLEARED");
      break;
    case mesh::OperationalEventCode::POWER_NORMAL:
      out.printf("POWER_NORMAL vbat=%ldmV", (long)record.value_a);
      break;
    case mesh::OperationalEventCode::POWER_ECONOMY:
      out.printf("POWER_ECONOMY vbat=%ldmV", (long)record.value_a);
      break;
    case mesh::OperationalEventCode::POWER_CRITICAL:
      out.printf("POWER_CRITICAL vbat=%ldmV low=%lds", (long)record.value_a,
                 (long)record.value_b);
      break;
    case mesh::OperationalEventCode::POWER_SHUTDOWN: {
      const uint8_t reason = (uint8_t)record.value_b;
      const uint32_t duration = (uint32_t)record.value_b >> 8;
      out.printf("POWER_SHUTDOWN reason=%s vbat=%ldmV low=%lus",
                 shutdownReasonName(reason), (long)record.value_a,
                 (unsigned long)duration);
      break;
    }
    case mesh::OperationalEventCode::POWER_RECOVERY_RESET:
      out.printf("POWER_RECOVERY_RESET vbat=%ldmV standby=%lds",
                 (long)record.value_a, (long)record.value_b);
      break;
    case mesh::OperationalEventCode::GPS_DETECTED:
      out.print("GPS_DETECTED");
      break;
    case mesh::OperationalEventCode::GPS_NOT_DETECTED:
      out.print("GPS_NOT_DETECTED");
      break;
    case mesh::OperationalEventCode::GPS_WINDOW_START:
      out.print("GPS_WINDOW_START");
      break;
    case mesh::OperationalEventCode::GPS_FIRST_FIX:
      out.printf("GPS_FIRST_FIX acquisition=%lds sats=%ld", (long)record.value_a,
                 (long)record.value_b);
      break;
    case mesh::OperationalEventCode::GPS_TIME_SYNC:
      out.printf("GPS_TIME_SYNC acquisition=%lds sats=%ld", (long)record.value_a,
                 (long)record.value_b);
      break;
    case mesh::OperationalEventCode::GPS_WINDOW_END:
      out.printf("GPS_WINDOW_END duration=%lds fix=%s", (long)record.value_a,
                 record.value_b ? "yes" : "no");
      break;
    case mesh::OperationalEventCode::GPS_OVERRIDE_START:
      if (record.value_b < 0) out.print("GPS_OVERRIDE_START continuous");
      else out.printf("GPS_OVERRIDE_START duration=%lds", (long)record.value_a);
      break;
    case mesh::OperationalEventCode::GPS_OVERRIDE_END: {
      const char* reason = record.value_b == 1 ? "expired" :
                           record.value_b == 2 ? "replaced" : "manual";
      out.printf("GPS_OVERRIDE_END elapsed=%lds reason=%s", (long)record.value_a,
                 reason);
      break;
    }
    default:
      out.printf("UNKNOWN event=%u a=%ld b=%ld", record.event,
                 (long)record.value_a, (long)record.value_b);
      break;
  }
}

void P1EventJournal::dump(Print& out) const {
  mesh::OperationalEventRecord records[MAX_RECORDS];
  const uint8_t count = loadRecords(records, MAX_RECORDS);
  for (uint8_t i = 1; i < count; i++) {
    const mesh::OperationalEventRecord key = records[i];
    int16_t j = (int16_t)i - 1;
    while (j >= 0 && mesh::operationalSequenceAfter(records[j].sequence,
                                                     key.sequence)) {
      records[j + 1] = records[j];
      j--;
    }
    records[j + 1] = key;
  }

  out.printf("P1 event log: %u/%u entries, write_errors=%u\n",
             count, MAX_RECORDS, write_errors);
  for (uint8_t i = 0; i < count; i++) {
    uint32_t display_epoch = records[i].epoch;
    bool trusted = (records[i].flags & mesh::OP_EVENT_TIME_TRUSTED) != 0;
    bool estimated = false;
    if (!trusted) {
      for (uint8_t anchor = i + 1; anchor < count; anchor++) {
        if (mesh::estimateOperationalEventEpoch(records[i], records[anchor],
                                                display_epoch)) {
          estimated = true;
          break;
        }
      }
    }

    out.printf("#%lu boot=%u U+%lus ",
               (unsigned long)records[i].sequence, records[i].boot_id,
               (unsigned long)records[i].uptime_seconds);
    if (trusted || estimated) {
      DateTime dt(display_epoch);
      out.printf("UTC=%04d-%02d-%02dT%02d:%02d:%02dZ%s ", dt.year(),
                 dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(),
                 estimated ? "~" : "");
    } else {
      out.print("UTC=? ");
    }
    if (records[i].flags & mesh::OP_EVENT_ERROR) out.print("ERROR ");
    printEventDetails(out, records[i]);
    out.println();
  }
}

bool P1EventJournal::clear() {
  if (!ready || !fs) return false;
  if (fs->exists(FILE_PATH) && !fs->remove(FILE_PATH)) return false;
  next_sequence = 1;
  next_slot = 0;
  entry_count = 0;
  return record(mesh::OperationalEventCode::LOG_CLEARED);
}

void P1EventJournal::formatStatus(char* status, size_t max_len) const {
  if (!status || max_len == 0) return;
  snprintf(status, max_len,
           "%s; entries %u/%u; boot %u; write_errors %u; UTC %s",
           ready ? "ready" : "unavailable", entry_count, MAX_RECORDS, boot_id,
           write_errors, time_trusted ? "trusted" : "waiting GPS");
}

#endif  // P1_EVENT_LOG
