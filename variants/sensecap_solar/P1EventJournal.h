#pragma once

#if defined(P1_EVENT_LOG)

#include <Adafruit_LittleFS.h>
#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/OperationalEventRecord.h>
#include <helpers/PowerStateMachine.h>
#include <helpers/SensorManager.h>

class P1EventJournal : public SensorRuntimeEventSink {
  static constexpr const char* FILE_PATH = "/p1_events.bin";
  static constexpr uint8_t MAX_RECORDS = 64;

  Adafruit_LittleFS* fs = nullptr;
  mesh::RTCClock* rtc = nullptr;
  uint32_t next_sequence = 1;
  uint16_t boot_id = 1;
  uint8_t next_slot = 0;
  uint8_t entry_count = 0;
  uint16_t write_errors = 0;
  bool ready = false;
  bool time_trusted = false;

  uint8_t loadRecords(mesh::OperationalEventRecord* records,
                      uint8_t capacity) const;
  bool recordInternal(mesh::OperationalEventCode code, int32_t value_a,
                      int32_t value_b, uint16_t flags, uint32_t epoch,
                      bool trusted_time);

public:
  enum InitError : int32_t {
    INIT_ERROR_RADIO = 1,
    INIT_ERROR_SENSORS = 2
  };

  bool begin(Adafruit_LittleFS& filesystem, mesh::RTCClock& clock);
  bool isReady() const { return ready; }

  bool record(mesh::OperationalEventCode code, int32_t value_a = 0,
              int32_t value_b = 0, uint16_t flags = 0);
  bool recordTrusted(mesh::OperationalEventCode code, uint32_t epoch,
                     int32_t value_a = 0, int32_t value_b = 0,
                     uint16_t flags = 0);

  void recordBoot(uint32_t reset_reason, uint8_t previous_shutdown_reason,
                  uint16_t boot_voltage_mv, bool external_power);
  void recordBootComplete(uint16_t battery_mv);
  void recordInitError(InitError error);
  void recordPowerState(mesh::PowerState state, uint16_t battery_mv,
                        uint32_t critical_seconds);
  void recordShutdown(uint8_t reason, uint16_t battery_mv,
                      uint32_t critical_seconds);
  void recordRecoveryReset(uint16_t battery_mv, uint32_t standby_seconds);
  void recordButtonState(uint8_t released_mask);
  void recordRadioReady(float frequency_mhz, int8_t tx_power_dbm,
                        uint8_t sf, uint8_t cr);

  void onSensorRuntimeEvent(const SensorRuntimeEvent& event) override;

  void dump(Print& out) const;
  bool clear();
  void formatStatus(char* status, size_t max_len) const;
};

extern P1EventJournal p1_event_journal;

#endif  // P1_EVENT_LOG
