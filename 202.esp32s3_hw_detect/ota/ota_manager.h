#ifndef OTA_OTA_MANAGER_H_
#define OTA_OTA_MANAGER_H_

#pragma once
#include <Arduino.h>

/**
 * @file ota_manager.h
 * @brief Web OTA helper (thin wrapper over the Arduino Update class).
 *
 * Update writes only to the OTA app partition, so NVS / WiFi / MQTT config and
 * the device ID survive the firmware swap. If the update is invalid or fails,
 * Update.end() returns false and the device keeps running the old firmware;
 * a watchdog/reboot restores the previous image.
 */
namespace RHD {

class OtaManager {
public:
    bool begin() { return true; }
    bool inProgress() const { return _inProgress; }

    bool  start(size_t size);
    size_t write(const uint8_t* data, size_t len);
    bool  end();
    const char* lastError() const { return _err; }

private:
    bool  _inProgress = false;
    const char* _err = "";
};

extern OtaManager g_ota;

} // namespace RHD

#endif // OTA_OTA_MANAGER_H_
