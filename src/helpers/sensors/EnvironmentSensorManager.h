#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/GpsSchedule.h>
#include <helpers/GpsOverride.h>
#include <helpers/sensors/LocationProvider.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool     gps_detected = false;
  bool     gps_active = false;
  bool     gps_power_save_mode = false;
  bool     gps_resume_after_power_save = false;
  uint32_t gps_update_interval_sec = 1;
  SensorRuntimeEventSink* event_sink = nullptr;
  void emitRuntimeEvent(SensorRuntimeEventType type, uint32_t epoch = 0,
                        uint32_t duration_ms = 0, int32_t value = 0) {
    if (!event_sink) return;
    const SensorRuntimeEvent event = {type, epoch, duration_ms, value};
    event_sink->onSensorRuntimeEvent(event);
  }

  #if ENV_INCLUDE_GPS && defined(GPS_SCHEDULE_PERIOD_SEC) && defined(GPS_SCHEDULE_WINDOW_SEC)
  mesh::GpsSchedule gps_schedule = mesh::GpsSchedule(
      (uint32_t)GPS_SCHEDULE_PERIOD_SEC * 1000U,
      (uint32_t)GPS_SCHEDULE_WINDOW_SEC * 1000U);
  mesh::GpsOverride gps_override;
  bool gps_schedule_window_open = false;
  bool gps_fix_advert_sent = false;
  bool gps_advert_pending = false;
  bool gps_window_had_fix = false;
  bool gps_time_sync_reported = false;
  uint32_t gps_window_started_ms = 0;
  uint32_t gps_powered_since_ms = 0;
  void updateGpsSchedule(uint32_t now_ms);
  #endif

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
  void setPowerSaveMode(bool enabled) override;
  void setEventSink(SensorRuntimeEventSink* sink) override { event_sink = sink; }
  bool consumeFreshLocation() override;
  bool getGpsScheduleStatus(char* status, size_t max_len) const override;
  bool handleGpsOverrideCommand(const char* argument, char* reply,
                                size_t max_len) override;
};
