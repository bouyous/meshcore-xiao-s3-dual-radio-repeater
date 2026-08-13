#pragma once

#include <stdint.h>

namespace mesh {

// Hardware-independent daily GPS policy. The first window starts when the
// schedule is enabled (normally just after boot), which also gives a freshly
// recovered solar node a chance to fix its clock and position immediately.
class GpsSchedule {
  uint32_t _period_ms;
  uint32_t _window_ms;
  mutable uint32_t _last_update_ms = 0;
  mutable uint32_t _cycle_elapsed_ms = 0;
  bool _enabled = false;
  bool _suppressed = false;

  uint32_t elapsedInCycle(uint32_t now_ms) const {
    if (_period_ms == 0) return 0;
    const uint32_t delta_ms = (uint32_t)(now_ms - _last_update_ms);
    _last_update_ms = now_ms;
    _cycle_elapsed_ms = (_cycle_elapsed_ms + (delta_ms % _period_ms)) % _period_ms;
    return _cycle_elapsed_ms;
  }

public:
  GpsSchedule(uint32_t period_ms, uint32_t window_ms)
    : _period_ms(period_ms),
      _window_ms(window_ms > period_ms ? period_ms : window_ms) {}

  void setEnabled(bool enabled, uint32_t now_ms) {
    if (enabled && !_enabled) {
      _last_update_ms = now_ms;
      _cycle_elapsed_ms = 0;
    }
    _enabled = enabled;
  }

  void setSuppressed(bool suppressed) { _suppressed = suppressed; }

  bool enabled() const { return _enabled; }
  bool suppressed() const { return _suppressed; }
  uint32_t periodSeconds() const { return _period_ms / 1000U; }
  uint32_t windowSeconds() const { return _window_ms / 1000U; }

  bool windowOpen(uint32_t now_ms) const {
    return _enabled && _period_ms > 0 && _window_ms > 0 &&
           elapsedInCycle(now_ms) < _window_ms;
  }

  bool shouldRun(uint32_t now_ms) const {
    return !_suppressed && windowOpen(now_ms);
  }

  uint32_t secondsRemaining(uint32_t now_ms) const {
    if (!windowOpen(now_ms)) return 0;
    return (_window_ms - elapsedInCycle(now_ms) + 999U) / 1000U;
  }

  uint32_t secondsUntilNextWindow(uint32_t now_ms) const {
    if (!_enabled || _period_ms == 0) return 0;
    if (windowOpen(now_ms)) return 0;
    return (_period_ms - elapsedInCycle(now_ms) + 999U) / 1000U;
  }
};

} // namespace mesh
