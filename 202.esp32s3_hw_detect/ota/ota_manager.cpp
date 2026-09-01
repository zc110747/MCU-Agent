#include "ota_manager.h"
#include "storage/log_manager.h"
#include <Update.h>

namespace RHD {

OtaManager g_ota;

bool OtaManager::start(size_t size) {
    _inProgress = true;
    if (!Update.begin(size)) {
        _err = "begin failed (size too large?)";
        LOG_ERROR("OTA", "%s", _err);
        _inProgress = false;
        return false;
    }
    _err = "";
    LOG_INFO("OTA", "update start, %u bytes", (unsigned)size);
    return true;
}

size_t OtaManager::write(const uint8_t* data, size_t len) {
    return Update.write(const_cast<uint8_t*>(data), len);
}

bool OtaManager::end() {
    bool ok = Update.end();
    _inProgress = false;
    if (ok) {
        LOG_INFO("OTA", "update OK, rebooting");
    } else {
        _err = "end failed (signature/space)";
        LOG_ERROR("OTA", "%s", _err);
    }
    return ok;
}

} // namespace RHD
