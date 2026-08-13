#pragma once

#include <stdint.h>

namespace mesh {

enum class PowerState : uint8_t {
  NORMAL = 0,
  ECONOMY,
  CRITICAL,
  SYSTEM_OFF
};

enum class PowerAction : uint8_t {
  NONE = 0,
  SHUTDOWN
};

struct PowerStateConfig {
  uint16_t warning_mv;
  uint16_t shutdown_mv;
  uint16_t critical_clear_mv;
  uint16_t warning_hysteresis_mv;
  uint32_t shutdown_delay_ms;
  uint16_t valid_min_mv;
  uint16_t valid_max_mv;
};

// Hardware-independent policy used by boards that monitor their battery at
// runtime. Keeping this class free of Arduino dependencies makes the safety
// policy deterministic and host-testable.
class PowerStateMachine {
  PowerStateConfig _config;
  PowerState _state = PowerState::NORMAL;
  bool _critical_timer_active = false;
  uint32_t _critical_since_ms = 0;
  uint16_t _last_valid_mv = 0;

public:
  explicit PowerStateMachine(const PowerStateConfig& config) : _config(config) {}

  PowerAction update(uint32_t now_ms, uint16_t battery_mv, bool externally_powered) {
    const bool valid_reading =
        battery_mv >= _config.valid_min_mv && battery_mv <= _config.valid_max_mv;

    if (externally_powered) {
      if (valid_reading) _last_valid_mv = battery_mv;
      _critical_timer_active = false;
      _state = PowerState::NORMAL;
      return PowerAction::NONE;
    }

    // Ignore impossible readings. A disconnected or unsettled ADC must never
    // be allowed to shut down a remotely installed repeater.
    if (!valid_reading) {
      // A missing sample breaks the proof that voltage stayed continuously
      // critical. Restart the safety delay on the next valid low reading.
      _critical_timer_active = false;
      return PowerAction::NONE;
    }

    _last_valid_mv = battery_mv;

    if (battery_mv < _config.shutdown_mv ||
        (_critical_timer_active && battery_mv < _config.critical_clear_mv)) {
      if (!_critical_timer_active) {
        _critical_timer_active = true;
        _critical_since_ms = now_ms;
      }
      _state = PowerState::CRITICAL;

      if ((uint32_t)(now_ms - _critical_since_ms) >= _config.shutdown_delay_ms) {
        _state = PowerState::SYSTEM_OFF;
        return PowerAction::SHUTDOWN;
      }
      return PowerAction::NONE;
    }

    _critical_timer_active = false;

    const uint32_t normal_threshold =
        (uint32_t)_config.warning_mv + _config.warning_hysteresis_mv;
    if (battery_mv < _config.warning_mv ||
        (_state != PowerState::NORMAL && battery_mv < normal_threshold)) {
      _state = PowerState::ECONOMY;
    } else {
      _state = PowerState::NORMAL;
    }

    return PowerAction::NONE;
  }

  PowerState state() const { return _state; }
  uint16_t lastValidMilliVolts() const { return _last_valid_mv; }

  uint32_t criticalDurationSeconds(uint32_t now_ms) const {
    return _critical_timer_active ? (uint32_t)(now_ms - _critical_since_ms) / 1000U : 0U;
  }

  bool isPowerSaving() const {
    return _state == PowerState::ECONOMY || _state == PowerState::CRITICAL ||
           _state == PowerState::SYSTEM_OFF;
  }

  static const char* stateName(PowerState state) {
    switch (state) {
      case PowerState::NORMAL:     return "normal";
      case PowerState::ECONOMY:    return "economy";
      case PowerState::CRITICAL:   return "critical";
      case PowerState::SYSTEM_OFF: return "systemoff";
    }
    return "unknown";
  }
};

} // namespace mesh
