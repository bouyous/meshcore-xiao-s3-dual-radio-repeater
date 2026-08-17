#pragma once

#include <stdint.h>

namespace mesh {

enum class GpsOverrideMode : uint8_t {
  OFF = 0,
  TIMED,
  CONTINUOUS
};

// Runtime-only GPS override. Timed modes use unsigned elapsed arithmetic so
// expiration remains correct across the 32-bit millis() wrap.
class GpsOverride {
  GpsOverrideMode _mode = GpsOverrideMode::OFF;
  uint32_t _started_ms = 0;
  uint32_t _duration_ms = 0;

public:
  void startTimed(uint32_t now_ms, uint32_t duration_ms) {
    _mode = duration_ms ? GpsOverrideMode::TIMED : GpsOverrideMode::OFF;
    _started_ms = now_ms;
    _duration_ms = duration_ms;
  }

  void startContinuous(uint32_t now_ms) {
    _mode = GpsOverrideMode::CONTINUOUS;
    _started_ms = now_ms;
    _duration_ms = 0;
  }

  void clear() {
    _mode = GpsOverrideMode::OFF;
    _duration_ms = 0;
  }

  bool expireIfNeeded(uint32_t now_ms) {
    if (_mode != GpsOverrideMode::TIMED ||
        (uint32_t)(now_ms - _started_ms) < _duration_ms) {
      return false;
    }
    clear();
    return true;
  }

  bool active(uint32_t now_ms) const {
    if (_mode == GpsOverrideMode::CONTINUOUS) return true;
    return _mode == GpsOverrideMode::TIMED &&
           (uint32_t)(now_ms - _started_ms) < _duration_ms;
  }

  GpsOverrideMode mode() const { return _mode; }

  uint32_t elapsedSeconds(uint32_t now_ms) const {
    if (_mode == GpsOverrideMode::OFF) return 0;
    return (uint32_t)(now_ms - _started_ms) / 1000U;
  }

  uint32_t remainingSeconds(uint32_t now_ms) const {
    if (_mode != GpsOverrideMode::TIMED) return 0;
    const uint32_t elapsed = (uint32_t)(now_ms - _started_ms);
    if (elapsed >= _duration_ms) return 0;
    return (_duration_ms - elapsed + 999U) / 1000U;
  }
};

}  // namespace mesh
