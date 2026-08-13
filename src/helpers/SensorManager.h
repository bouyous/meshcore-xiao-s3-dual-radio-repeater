#pragma once

#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

enum class SensorRuntimeEventType : uint8_t {
  GPS_DETECTED = 1,
  GPS_NOT_DETECTED,
  GPS_WINDOW_START,
  GPS_FIRST_FIX,
  GPS_TIME_SYNC,
  GPS_WINDOW_END,
  GPS_OVERRIDE_START,
  GPS_OVERRIDE_END
};

struct SensorRuntimeEvent {
  SensorRuntimeEventType type;
  uint32_t epoch;
  uint32_t duration_ms;
  int32_t value;
};

class SensorRuntimeEventSink {
public:
  virtual ~SensorRuntimeEventSink() = default;
  virtual void onSensorRuntimeEvent(const SensorRuntimeEvent& event) = 0;
};

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual void setEventSink(SensorRuntimeEventSink* sink) { (void)sink; }
  virtual void setPowerSaveMode(bool enabled) { (void)enabled; }
  virtual bool consumeFreshLocation() { return false; }
  virtual bool getGpsScheduleStatus(char* status, size_t max_len) const {
    (void)status;
    (void)max_len;
    return false;
  }
  virtual bool handleGpsOverrideCommand(const char* argument, char* reply,
                                        size_t max_len) {
    (void)argument;
    (void)reply;
    (void)max_len;
    return false;
  }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual const char* getSettingValue(int i) const { return NULL; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
