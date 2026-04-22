#include "NodeNaming.h"

#include <WiFi.h>

#include "config.h"
#include "utils.h"

bool NodeNaming::chooseDisplayName(char* destination, size_t destinationSize) const {
    if (destination == nullptr || destinationSize == 0) {
        return false;
    }

    int highestIndex = 0;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(25);

    const int networksFound = WiFi.scanNetworks(false, true);
    for (int index = 0; index < networksFound; ++index) {
        const String ssid = WiFi.SSID(index);
        if (!utils::startsWithIgnoreCase(ssid, cfg::kNodeNamePrefix)) {
            continue;
        }
        const String suffix = ssid.substring(strlen(cfg::kNodeNamePrefix));
        const int parsed = suffix.toInt();
        if (parsed > highestIndex) {
            highestIndex = parsed;
        }
    }
    WiFi.scanDelete();

    snprintf(destination, destinationSize, "%s%d", cfg::kNodeNamePrefix, highestIndex + 1);
    return true;
}
