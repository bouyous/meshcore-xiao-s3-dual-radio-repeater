#pragma once

#include <stdint.h>
#include <string.h>

namespace mesh {

enum class GpsPowerGuardMode : uint8_t {
  ECONOMY = 0,
  CRITICAL,
  OFF
};

// Selects which battery state is allowed to suppress the GPS.  The OFF mode
// disables only this accessory guard: the board's final low-voltage SYSTEMOFF
// protection remains authoritative.
class GpsPowerGuard {
public:
  static const char* modeName(GpsPowerGuardMode mode) {
    switch (mode) {
      case GpsPowerGuardMode::ECONOMY: return "economy";
      case GpsPowerGuardMode::CRITICAL: return "critical";
      case GpsPowerGuardMode::OFF: return "off";
    }
    return "economy";
  }

  static bool parseMode(const char* text, GpsPowerGuardMode& mode) {
    if (!text) return false;
    if (strcmp(text, "economy") == 0) {
      mode = GpsPowerGuardMode::ECONOMY;
      return true;
    }
    if (strcmp(text, "critical") == 0) {
      mode = GpsPowerGuardMode::CRITICAL;
      return true;
    }
    if (strcmp(text, "off") == 0) {
      mode = GpsPowerGuardMode::OFF;
      return true;
    }
    return false;
  }

  static bool shouldSuppress(GpsPowerGuardMode mode, const char* power_state) {
    if (!power_state) return false;
    // Stop accessories during the final radio grace period in every mode.
    if (strcmp(power_state, "systemoff") == 0) return true;
    if (mode == GpsPowerGuardMode::OFF) return false;
    if (strcmp(power_state, "critical") == 0) return true;
    return mode == GpsPowerGuardMode::ECONOMY &&
           strcmp(power_state, "economy") == 0;
  }
};

}  // namespace mesh
