#include "utils.h"

#include <ctype.h>

#include "mesh_types.h"

namespace utils {

void safeCopy(char* destination, size_t destinationSize, const char* source) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

void formatHexToken(char* destination, size_t destinationSize, size_t randomBytes) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }
    destination[0] = '\0';
    for (size_t index = 0; index < randomBytes && (index * 2U + 2U) < destinationSize; ++index) {
        char chunk[3];
        snprintf(chunk, sizeof(chunk), "%02X", static_cast<unsigned>(esp_random() & 0xFFU));
        strncat(destination, chunk, destinationSize - strlen(destination) - 1);
    }
}

void formatNodeId(char* destination, size_t destinationSize, uint64_t efuseMac) {
    snprintf(destination, destinationSize, "N-%012llX", static_cast<unsigned long long>(efuseMac & 0xFFFFFFFFFFFFULL));
}

void formatMessageId(char* destination, size_t destinationSize, const char* nodeId, uint32_t sequence) {
    const char* suffix = (nodeId != nullptr && strlen(nodeId) > 2) ? nodeId + 2 : "NODE";
    snprintf(destination, destinationSize, "M-%s-%08lX", suffix, static_cast<unsigned long>(sequence));
}

uint32_t fnv1a32(const uint8_t* data, size_t size) {
    uint32_t hash = 2166136261UL;
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619UL;
    }
    return hash;
}

uint32_t fnv1a32(const char* value) {
    if (value == nullptr) {
        return 0;
    }
    return fnv1a32(reinterpret_cast<const uint8_t*>(value), strlen(value));
}

bool startsWithIgnoreCase(const String& value, const char* prefix) {
    if (prefix == nullptr) {
        return false;
    }
    const size_t prefixLength = strlen(prefix);
    if (value.length() < prefixLength) {
        return false;
    }
    for (size_t index = 0; index < prefixLength; ++index) {
        if (tolower(static_cast<unsigned char>(value[index])) != tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

bool isAllowedUsername(const char* username) {
    if (username == nullptr) {
        return false;
    }
    const size_t length = strlen(username);
    if (length < 3 || length >= cfg::kUsernameLength) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const char symbol = username[index];
        if (!(isalnum(static_cast<unsigned char>(symbol)) || symbol == '-' || symbol == '_')) {
            return false;
        }
    }
    return true;
}

bool constantEquals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return strncmp(left, right, cfg::kUsernameLength) == 0;
}

void uppercase(char* value) {
    if (value == nullptr) {
        return;
    }
    for (size_t index = 0; value[index] != '\0'; ++index) {
        value[index] = static_cast<char>(toupper(static_cast<unsigned char>(value[index])));
    }
}

const char* deliveryFailureToCode(uint8_t failure) {
    switch (static_cast<DeliveryFailure>(failure)) {
        case DeliveryFailure::UserOffline:
            return "USER_OFFLINE";
        case DeliveryFailure::RouteNotFound:
            return "ROUTE_NOT_FOUND";
        case DeliveryFailure::QueueFull:
            return "QUEUE_FULL";
        case DeliveryFailure::PayloadTooLarge:
            return "PAYLOAD_TOO_LARGE";
        case DeliveryFailure::Timeout:
            return "TIMEOUT";
        case DeliveryFailure::InvalidSession:
            return "INVALID_SESSION";
        case DeliveryFailure::None:
        default:
            return "UNKNOWN";
    }
}

uint32_t jitterMs(uint32_t minValue, uint32_t maxValue) {
    if (maxValue <= minValue) {
        return minValue;
    }
    return minValue + (esp_random() % (maxValue - minValue + 1UL));
}

bool timeAfterOrEqual(uint32_t nowMs, uint32_t referenceMs) {
    return static_cast<int32_t>(nowMs - referenceMs) >= 0;
}

uint32_t elapsedMs(uint32_t nowMs, uint32_t thenMs, bool* clampedFuture) {
    const bool isFuture = !timeAfterOrEqual(nowMs, thenMs);
    if (clampedFuture != nullptr) {
        *clampedFuture = isFuture;
    }
    if (isFuture) {
        return 0;
    }
    return nowMs - thenMs;
}

bool elapsedReached(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs, bool* clampedFuture) {
    return elapsedMs(nowMs, thenMs, clampedFuture) >= intervalMs;
}

uint32_t remainingMs(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs, bool* clampedFuture) {
    const uint32_t elapsed = elapsedMs(nowMs, thenMs, clampedFuture);
    if (elapsed >= intervalMs) {
        return 0;
    }
    return intervalMs - elapsed;
}

}  // namespace utils
