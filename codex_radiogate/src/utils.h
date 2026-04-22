#pragma once

#include <Arduino.h>
#include "config.h"

namespace utils {

void safeCopy(char* destination, size_t destinationSize, const char* source);
void formatHexToken(char* destination, size_t destinationSize, size_t randomBytes);
void formatNodeId(char* destination, size_t destinationSize, uint64_t efuseMac);
void formatMessageId(char* destination, size_t destinationSize, const char* nodeId, uint32_t sequence);
uint32_t fnv1a32(const uint8_t* data, size_t size);
uint32_t fnv1a32(const char* value);
bool startsWithIgnoreCase(const String& value, const char* prefix);
bool isAllowedUsername(const char* username);
bool constantEquals(const char* left, const char* right);
void uppercase(char* value);
const char* deliveryFailureToCode(uint8_t failure);
uint32_t jitterMs(uint32_t minValue, uint32_t maxValue);
bool timeAfterOrEqual(uint32_t nowMs, uint32_t referenceMs);
uint32_t elapsedMs(uint32_t nowMs, uint32_t thenMs, bool* clampedFuture = nullptr);
bool elapsedReached(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs, bool* clampedFuture = nullptr);
uint32_t remainingMs(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs, bool* clampedFuture = nullptr);

}  // namespace utils
