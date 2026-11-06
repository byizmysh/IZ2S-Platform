#pragma once
#include <stdint.h>

// Reannounce an idle keyboard when the host has not configured it. A configured
// (including suspended) device is left alone. No input job belongs to this policy.
class UsbRecoveryPolicy {
public:
  enum Action { None, Detach, Attach };
  Action update(uint32_t now, bool mounted, bool busy, bool panelPresent) {
    if (_detached) {
      if (uint32_t(now - _since) < 400) return None;
      _detached = false;
      _since = now;
      return Attach;
    }
    if (mounted || busy || !panelPresent) {
      _armed = false;
      if (mounted) _backoff = 0;
      return None;
    }
    if (!_armed) { _armed = true; _since = now; return None; }
    const uint32_t waits[] = {8000, 16000, 32000, 60000};
    if (uint32_t(now - _since) < waits[_backoff]) return None;
    if (_backoff < 3) ++_backoff;
    ++_attempts;
    _detached = true;
    _since = now;
    return Detach;
  }
  bool detached() const { return _detached; }
  uint32_t attempts() const { return _attempts; }
private:
  bool _armed = false, _detached = false;
  uint8_t _backoff = 0;
  uint32_t _since = 0, _attempts = 0;
};
