#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mesh {

// Small fixed-capacity list used by unattended nodes for encrypted operational
// alerts. Keeping parsing and list mutation independent from Arduino makes the
// safety-critical CLI validation host-testable.
template <size_t Capacity>
class AlertRecipientList {
  uint8_t _keys[Capacity][32] = {};
  size_t _count = 0;

  static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

public:
  static constexpr size_t KEY_BYTES = 32;
  static constexpr size_t KEY_HEX_CHARS = KEY_BYTES * 2;

  static bool decodeHexKey(const char* hex, uint8_t key[KEY_BYTES]) {
    if (!hex || strlen(hex) != KEY_HEX_CHARS) return false;
    for (size_t i = 0; i < KEY_BYTES; i++) {
      const int high = hexNibble(hex[i * 2]);
      const int low = hexNibble(hex[i * 2 + 1]);
      if (high < 0 || low < 0) return false;
      key[i] = (uint8_t)((high << 4) | low);
    }
    return true;
  }

  static void encodeHexKey(const uint8_t key[KEY_BYTES],
                           char hex[KEY_HEX_CHARS + 1]) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < KEY_BYTES; i++) {
      hex[i * 2] = digits[key[i] >> 4];
      hex[i * 2 + 1] = digits[key[i] & 0x0F];
    }
    hex[KEY_HEX_CHARS] = '\0';
  }

  size_t count() const { return _count; }
  constexpr size_t capacity() const { return Capacity; }
  const uint8_t* keyAt(size_t index) const {
    return index < _count ? _keys[index] : nullptr;
  }

  bool contains(const uint8_t key[KEY_BYTES]) const {
    for (size_t i = 0; i < _count; i++) {
      if (memcmp(_keys[i], key, KEY_BYTES) == 0) return true;
    }
    return false;
  }

  bool add(const uint8_t key[KEY_BYTES]) {
    if (!key || contains(key) || _count >= Capacity) return false;
    memcpy(_keys[_count++], key, KEY_BYTES);
    return true;
  }

  bool addHex(const char* hex) {
    uint8_t key[KEY_BYTES];
    return decodeHexKey(hex, key) && add(key);
  }

  bool removeAt(size_t index) {
    if (index >= _count) return false;
    for (size_t i = index + 1; i < _count; i++) {
      memcpy(_keys[i - 1], _keys[i], KEY_BYTES);
    }
    memset(_keys[--_count], 0, KEY_BYTES);
    return true;
  }

  bool remove(const uint8_t key[KEY_BYTES]) {
    for (size_t i = 0; i < _count; i++) {
      if (memcmp(_keys[i], key, KEY_BYTES) == 0) return removeAt(i);
    }
    return false;
  }

  bool removeHex(const char* hex) {
    uint8_t key[KEY_BYTES];
    return decodeHexKey(hex, key) && remove(key);
  }

  void clear() {
    memset(_keys, 0, sizeof(_keys));
    _count = 0;
  }
};

}  // namespace mesh
