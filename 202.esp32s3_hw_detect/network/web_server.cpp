#include "web_server.h"
#include "network/web_pages.h"
#include "app/debug_gateway.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"
#include "ota/ota_manager.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

namespace RHD {

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static String base64Decode(const String& in) {
    String out;
    int i = 0, len = in.length();
    while (i + 3 < len + 1) {
        int v = 0, n = 0;
        for (int j = 0; j < 4; ++j) {
            if (i + j >= len || in[i + j] == '=') { v <<= 6; continue; }
            const char* p = strchr(B64, in[i + j]);
            v = (v << 6) | (p ? (p - B64) : 0); n++;
        }
        for (int j = 2; j >= 0 && n > 1; --j, n--) out += (char)((v >> (j * 8)) & 0xFF);
        i += 4;
    }
    return out;
}

bool WebServerManager::begin() {
    auto* srv = new WebServer(AppConfig::HTTP_PORT);
    _server = srv;

    srv->on("/", [this]() { handleRoot(); });
    srv->on("/api/status", [this]() { handleStatus(); });
    srv->on("/api/uart/config", HTTP_GET,  [this]() { handleUartConfig(); });
    srv->on("/api/uart/config", HTTP_POST, [this]() { handleUartConfig(); });
    srv->on("/api/adc/config",  HTTP_GET,  [this]() { handleAdcConfig(); });
    srv->on("/api/adc/config",  HTTP_POST, [this]() { handleAdcConfig(); });
    srv->on("/api/adc/read",    HTTP_POST, [this]() { handleAdcRead(); });
    srv->on("/api/gpio",        HTTP_GET,  [this]() { handleGpio(); });
    srv->on("/api/gpio",        HTTP_POST, [this]() { handleGpio(); });
    srv->on("/api/wifi",        HTTP_GET,  [this]() { handleWifi(); });
    srv->on("/api/wifi",        HTTP_POST, [this]() { handleWifi(); });
    srv->on("/api/mqtt",        HTTP_GET,  [this]() { handleMqtt(); });
    srv->on("/api/mqtt",        HTTP_POST, [this]() { handleMqtt(); });
    srv->on("/api/webpw",       HTTP_POST, [this]() { handleWebPw(); });
    srv->on("/api/command",     HTTP_POST, [this]() { handleCommand(); });
    srv->on("/api/reset",       HTTP_POST, [this]() { handleReset(); });
    srv->on("/api/logs",        [this]() { handleLogs(); });
    srv->on("/update", HTTP_POST, [this]() { handleUpdateFinish(); }, [this]() { handleUpdateUpload(); });

    srv->begin();
    _ready = true;
    LOG_INFO("Web", "server on port %u", AppConfig::HTTP_PORT);

    xTaskCreatePinnedToCore(&WebServerManager::taskTrampoline, "web_task",
                            AppConfig::WEB_TASK_STACK + 2048, this,
                            AppConfig::WEB_TASK_PRIO, &_task,
                            AppConfig::WEB_TASK_CORE);
    return true;
}

void WebServerManager::taskTrampoline(void* arg) {
    static_cast<WebServerManager*>(arg)->taskLoop();
}
void WebServerManager::taskLoop() {
    auto* srv = static_cast<WebServer*>(_server);
    while (true) {
        srv->handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool WebServerManager::authenticated() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->hasHeader("Authorization")) {
        String h = srv->header("Authorization");
        if (h.startsWith("Basic ")) {
            String dec = base64Decode(h.substring(6));
            int c = dec.indexOf(':');
            String user = dec.substring(0, c);
            String pass = dec.substring(c + 1);
            if (user == "admin" && pass == g_config.webPassword()) return true;
        }
    }
    if (srv->hasArg("pw") && srv->arg("pw") == g_config.webPassword()) return true;
    srv->sendHeader("WWW-Authenticate", "Basic realm=\"debugger\"");
    srv->send(401, "text/plain", "Unauthorized");
    return false;
}

void WebServerManager::sendJson(JsonDocument& d, int code) {
    auto* srv = static_cast<WebServer*>(_server);
    String s;
    serializeJson(d, s);
    srv->send(code, "application/json", s);
}

void WebServerManager::handleRoot() {
    auto* srv = static_cast<WebServer*>(_server);
    srv->send(200, "text/html", DASHBOARD_HTML);
}

void WebServerManager::handleStatus() {
    auto* srv = static_cast<WebServer*>(_server);
    JsonDocument d;
    d["device"]        = g_config.deviceId();
    d["firmware"]      = AppConfig::FIRMWARE_VERSION;
    d["uptime"]        = millis() / 1000;
    d["free_heap"]     = ESP.getFreeHeap();
    d["min_heap"]      = ESP.getMinFreeHeap();
    d["wifi"]          = getGateway().wifi()->modeStr();
    d["wifi_rssi"]     = getGateway().wifi()->rssi();
    d["ip"]            = getGateway().wifi()->ip();
    d["mqtt"]          = getGateway().mqtt()->connected() ? "connected" : "down";
    d["uart_drops"]    = getGateway().uart()->dropCount();
    d["adc_drops"]     = getGateway().adc()->dropCount();
    d["wifi_ssid"]     = g_config.wifiSsid();
    d["mqtt_broker"]   = g_config.mqttBroker();
    d["mqtt_port"]     = g_config.mqttPort();
    d["mqtt_user"]     = g_config.mqttUser();
    d["mqtt_keep"]     = g_config.mqttKeepAlive();
    d["mqtt_tls"]      = g_config.mqttTls();
    UartConfig uc = g_config.uartConfig();
    d["uart_baud"] = uc.baud; d["uart_data"] = uc.dataBits;
    d["uart_stop"] = uc.stopBits; d["uart_par"] = uc.parity;
    d["adc_fsr"]   = g_config.adcFsrVolts();
    d["ws_port"]   = AppConfig::WEBSOCKET_PORT;
    d["chip"]      = ESP.getChipModel();
    d["cpu_mhz"]   = ESP.getCpuFreqMHz();
    d["uart_ready"]= getGateway().uart()->isReady();
    d["adc_ready"] = getGateway().adc()->isReady();
    d["gpio_ready"]= getGateway().gpio()->isReady();
    d["led_ready"] = getGateway().led()->isReady();
    d["pwm_active"]= getGateway().pwm()->isActive();
    d["pwm_pin"]   = getGateway().pwm()->pin();
    d["pwm_period"]= getGateway().pwm()->periodUs();
    d["pwm_duty"]  = getGateway().pwm()->dutyPct();
    sendJson(d);
}

void WebServerManager::handleUartConfig() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->method() == HTTP_GET) {
        JsonDocument d;
        UartConfig uc = g_config.uartConfig();
        d["baud"] = uc.baud; d["data"] = uc.dataBits; d["stop"] = uc.stopBits; d["parity"] = uc.parity;
        sendJson(d);
        return;
    }
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    unsigned long baud = d["baud"] | 115200;
    uint8_t db = d["data"] | 8, sb = d["stop"] | 1, par = d["parity"] | 0;
    getGateway().uart()->configure(baud, db, sb, par);
    UartConfig uc{baud, db, sb, par};
    g_config.setUart(uc); g_config.save();
    srv->send(200, "text/plain", "uart saved");
}

void WebServerManager::handleAdcConfig() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->method() == HTTP_GET) {
        JsonDocument d;
        d["fsr"] = g_config.adcFsrVolts();
        d["divider0"] = g_config.adcDivider(0); d["divider1"] = g_config.adcDivider(1);
        d["divider2"] = g_config.adcDivider(2); d["divider3"] = g_config.adcDivider(3);
        sendJson(d);
        return;
    }
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    float fsr = d["fsr"] | 6.144f;
    float div[4] = { d["d0"] | 1.0f, d["d1"] | 1.0f, d["d2"] | 1.0f, d["d3"] | 1.0f };
    float off[4] = {0,0,0,0};
    getGateway().adc()->configure(fsr, div, off);
    g_config.setAdc(fsr, div, off); g_config.save();
    srv->send(200, "text/plain", "adc saved");
}

void WebServerManager::handleAdcRead() {
    auto* srv = static_cast<WebServer*>(_server);
    if (!authenticated()) return;
    uint16_t raw[4]; float volt[4];
    getGateway().adc()->latest(raw, volt);
    JsonDocument d;
    for (int i = 0; i < 4; ++i) {
        d["ch" + String(i)]["raw"] = raw[i];
        d["ch" + String(i)]["voltage"] = volt[i];
    }
    sendJson(d);
}

void WebServerManager::handleGpio() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->method() == HTTP_GET) {
        JsonDocument d;
        uint8_t st[DebugPins::GPIO_MONITOR_COUNT], dir[DebugPins::GPIO_MONITOR_COUNT];
        getGateway().gpio()->states(st, dir);
        JsonArray arr = d.createNestedArray("gpios");
        for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
            JsonObject o = arr.createNestedObject();
            o["pin"] = DebugPins::GPIO_MONITOR_PINS[i];
            o["state"] = st[i];
            o["dir"] = dir[i];
        }
        d["led_mode"] = getGateway().led()->modeStr();
        d["led_pin"]  = DebugPins::RUN_LED;
        JsonObject led = d.createNestedObject("led_state");
        led["mode"]     = (int)getGateway().led()->mode();
        led["mode_str"] = getGateway().led()->modeStr();
        led["on"]       = getGateway().led()->isOn() ? 1 : 0;
        led["r"]        = getGateway().led()->red();
        led["g"]        = getGateway().led()->green();
        led["b"]        = getGateway().led()->blue();
        JsonObject pwm = d.createNestedObject("pwm");
        pwm["active"] = getGateway().pwm()->isActive();
        pwm["pin"]    = getGateway().pwm()->pin();
        pwm["period"] = getGateway().pwm()->periodUs();
        pwm["duty"]   = getGateway().pwm()->dutyPct();
        pwm["freq"]   = getGateway().pwm()->freqHz();
        {
            uint16_t raw[4]; float volt[4];
            getGateway().adc()->latest(raw, volt);
            JsonArray adc = d.createNestedArray("adc");
            for (int i = 0; i < AppConfig::ADC_CHANNEL_COUNT; ++i) {
                JsonObject o = adc.createNestedObject();
                o["ch"] = i; o["raw"] = raw[i]; o["voltage"] = volt[i];
            }
            d["adc_src"] = getGateway().adc()->adcSource();
        }
        sendJson(d);
        return;
    }
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    uint8_t pin = d["gpio"] | 0; uint8_t val = d["value"] | 0;
    bool ok = getGateway().gpio()->setPin(pin, val);
    srv->send(ok ? 200 : 400, "text/plain", ok ? "ok" : "rejected");
}

void WebServerManager::handleWifi() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->method() == HTTP_GET) {
        JsonDocument d;
        d["ssid"] = g_config.wifiSsid(); // password intentionally omitted
        sendJson(d);
        return;
    }
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    String ssid = d["ssid"] | "";
    String pass = d["pass"] | "";
    g_config.setWifi(ssid, pass); g_config.save();
    getGateway().wifi()->applySta(ssid, pass);
    srv->send(200, "text/plain", "wifi saved, reconnecting");
}

void WebServerManager::handleMqtt() {
    auto* srv = static_cast<WebServer*>(_server);
    if (srv->method() == HTTP_GET) {
        JsonDocument d;
        d["broker"] = g_config.mqttBroker();
        d["port"] = g_config.mqttPort();
        d["user"] = g_config.mqttUser();
        d["keep"] = g_config.mqttKeepAlive();
        d["tls"] = g_config.mqttTls();
        sendJson(d); // password omitted
        return;
    }
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    String broker = d["broker"] | "";
    int port = d["port"] | 1883;
    String user = d["user"] | "";
    String pass = d["pass"] | "";
    int keep = d["keep"] | 30;
    bool tls = d["tls"] | false;
    g_config.setMqtt(broker, port, user, pass, keep, tls); g_config.save();
    getGateway().mqtt()->reconnect();
    srv->send(200, "text/plain", "mqtt saved, reconnecting");
}

void WebServerManager::handleWebPw() {
    auto* srv = static_cast<WebServer*>(_server);
    if (!authenticated()) return;
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    String np = d["newpw"] | "";
    if (np.length() == 0) { srv->send(400, "text/plain", "empty"); return; }
    g_config.setWebPassword(np); g_config.save();
    srv->send(200, "text/plain", "web password changed");
}

void WebServerManager::handleCommand() {
    auto* srv = static_cast<WebServer*>(_server);
    JsonDocument d;
    if (deserializeJson(d, srv->arg("plain")) != DeserializationError::Ok) {
        srv->send(400, "text/plain", "bad json"); return;
    }
    getGateway().handleJsonCommand(d.as<JsonObjectConst>());
    srv->send(200, "text/plain", "ok");
}

void WebServerManager::handleReset() {
    auto* srv = static_cast<WebServer*>(_server);
    if (!authenticated()) return;
    srv->send(200, "text/plain", "rebooting");
    delay(200);
    ESP.restart();
}

void WebServerManager::handleLogs() {
    auto* srv = static_cast<WebServer*>(_server);
    String out;
    g_log.dump(out);
    srv->send(200, "text/plain", out);
}

void WebServerManager::handleUpdateUpload() {
    auto* srv = static_cast<WebServer*>(_server);
    HTTPUpload& up = srv->upload();
    if (up.status == UPLOAD_FILE_START) {
        _otaAuthOk = authenticated();   // sends 401 if it fails
        if (_otaAuthOk) g_ota.start(up.totalSize);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_otaAuthOk && g_ota.inProgress()) g_ota.write(up.buf, up.currentSize);
    }
}

void WebServerManager::handleUpdateFinish() {
    auto* srv = static_cast<WebServer*>(_server);
    if (!_otaAuthOk) return;            // 401 already sent during upload start
    bool ok = g_ota.end();
    srv->send(ok ? 200 : 500, "text/plain", ok ? "OTA OK, rebooting" : "OTA failed");
    if (ok) { delay(200); ESP.restart(); }
}

} // namespace RHD
