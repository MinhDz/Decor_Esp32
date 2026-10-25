#pragma once
#include <Arduino.h>

// ============================================================================
// BUILT-IN EMBEDDED EMOJI DATASET (Zero-FS Fallback)
// Expose helper functions only to keep header lightweight across translation units
// ============================================================================

namespace BuiltinEmojis {
  const uint8_t* getCuteGirlJpg(const String& emo, size_t& outLen);
  const uint8_t* getDoiMatJpg(const String& emo, size_t& outLen);
}
