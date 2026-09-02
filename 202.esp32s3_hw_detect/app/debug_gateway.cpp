#include "debug_gateway.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "network/wifi_manager.h"
#include "network/mqtt_manager.h"
#include "network/websocket_manager.h"
#include "network/web_server.h"
#include "debug/uart_monitor.h"
#include "debug/adc_monitor.h"
#include "debug/gpio_monitor.h"
#include "debug/ws2812_led.h"
#include "debug/pwm_output.h"
#include "ota/ota_manager.h"
#include <ArduinoJson.h>
#include <WiFi.h>

namespace RHD {

// ---- WebSocket log sink (called from any task via LOG_*) ----
void logToWebSocket(const char* line) { g_ws.enqueueLog(line); }

DebugGateway& getGateway() {
    static DebugGateway g;
    return g;
}

bool DebugGateway::begin() {
    // 1. Serial (USB CDC == the reserved GPIO19/20) for logs
    Serial.begin(115200);
    delay(50);

    // 2. Config (NVS)
    g_config.begin();

    // 3. Device ID from MAC (independent of WiFi driver)
    g_config.computeDeviceId();

    // 4. Log manager + live WebSocket forwarding
    g_log.begin();
    g_log.setSink(&logToWebSocket);

    // 5. Event bus
    g_eventBus.begin();

    LOG_INFO("SYS", "=== ESP32-S3 Remote Hardware Debugger ===");
    LOG_INFO("SYS", "Firmware %s  Device %s", AppConfig::FIRMWARE_VERSION,
             g_config.deviceId().c_str());

    // 6. Debug modules
    _uart = new UartMonitor();
    _adc  = new AdcMonitor();
    _gpio = new GpioMonitor();
    _led  = new Ws2812Controller();
    _pwm  = new PwmController();
    if (!_uart->begin()) LOG_ERROR("SYS", "UART module failed");
    if (!_adc->begin())  LOG_ERROR("SYS", "ADC module failed");
    if (!_gpio->begin()) LOG_ERROR("SYS", "GPIO module failed");
    if (!_led->begin())  LOG_ERROR("SYS", "LED module failed");
    if (!_pwm->begin())  LOG_ERROR("SYS", "PWM module failed");

    // 7. GPIO allocation report (after all claims)
    DebugPins::g_pinManager.printAllocation();

    // 8. Network
    _wifi = new WiFiManager();
    _wifi->begin();

    _ws = new WebSocketManager();
    _ws->begin();

    _mqtt = new MqttManager();
    _mqtt->begin();

    _web = new WebServerManager();
    _web->begin();

    _ota = new OtaManager();
    _ota->begin();

    // 9. Event fan-out task
    startEventTask();

    // 10. Heap snapshot
    LOG_INFO("SYS", "Heap free=%u min=%u largest=%u",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

    // 11. Ready banner
    LOG_INFO("SYS", "=================================");
    LOG_INFO("SYS", "Firmware : %s", AppConfig::FIRMWARE_VERSION);
    LOG_INFO("SYS", "Device ID: %s", g_config.deviceId().c_str());
    LOG_INFO("SYS", "WiFi     : %s", _wifi->modeStr().c_str());
    LOG_INFO("SYS", "IP       : %s", _wifi->ip().c_str());
    LOG_INFO("SYS", "MQTT     : %s", _mqtt->connected() ? "connected" : "down");
    LOG_INFO("SYS", "UART     : %s", _uart->isReady() ? "ready" : "down");
    LOG_INFO("SYS", "ADC      : %s", _adc->isReady() ? "ready" : "down");
    LOG_INFO("SYS", "GPIO     : %s", _gpio->isReady() ? "ready" : "down");
    LOG_INFO("SYS", "LED      : %s (%s)", _led->isReady() ? "ready" : "down",
             _led->modeStr());
    LOG_INFO("SYS", "PWM      : %s", _pwm->isReady() ? "ready" : "down");
    LOG_INFO("SYS", "Web      : ready :%u", AppConfig::HTTP_PORT);
    LOG_INFO("SYS", "WebSocket: ready :%u", AppConfig::WEBSOCKET_PORT);
    LOG_INFO("SYS", "OTA      : ready");
    LOG_INFO("SYS", "System Ready");
    LOG_INFO("SYS", "=================================");

    return true;
}

void DebugGateway::startEventTask() {
    xTaskCreatePinnedToCore(&DebugGateway::eventTaskTrampoline, "event_task",
                            AppConfig::EVENT_TASK_STACK, this,
                            AppConfig::EVENT_TASK_PRIO, &_eventTask,
                            AppConfig::EVENT_TASK_CORE);
}

void DebugGateway::eventTaskTrampoline(void* arg) {
    static_cast<DebugGateway*>(arg)->eventTaskLoop();
}

void DebugGateway::eventTaskLoop() {
    DebugEvent ev;
    _lastStatus = millis();
    while (true) {
        if (g_eventBus.pop(ev, pdMS_TO_TICKS(100))) {
            g_ws.enqueue(ev);
            g_mqtt.enqueue(ev);
        }
        if (millis() - _lastStatus >= AppConfig::SYSTEM_STATUS_INTERVAL_MS) {
            _lastStatus = millis();
            publishSystemStatus();
        }
    }
}

void DebugGateway::publishSystemStatus() {
    char buf[600];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"system\",\"device\":\"%s\",\"firmware\":\"%s\",\"uptime\":%u,"
        "\"chip\":\"%s\",\"cpu_mhz\":%u,"
        "\"free_heap\":%u,\"min_heap\":%u,"
        "\"wifi\":\"%s\",\"wifi_rssi\":%d,\"ip\":\"%s\","
        "\"mqtt\":\"%s\",\"mqtt_broker\":\"%s\",\"mqtt_port\":%u,"
        "\"uart_ready\":%d,\"adc_ready\":%d,\"adc_src\":\"%s\",\"gpio_ready\":%d,"
        "\"led_ready\":%d,\"led_mode\":\"%s\","
        "\"pwm_active\":%d,\"pwm_pin\":%d,\"pwm_period\":%u,\"pwm_duty\":%u,"
        "\"uart_drops\":%u,\"adc_drops\":%u}",
        g_config.deviceId().c_str(),
        AppConfig::FIRMWARE_VERSION,
        millis() / 1000,
        ESP.getChipModel(),
        ESP.getCpuFreqMHz(),
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        _wifi ? _wifi->modeStr().c_str() : "none",
        _wifi ? _wifi->rssi() : 0,
        _wifi ? _wifi->ip().c_str() : "",
        _mqtt ? (_mqtt->connected() ? "connected" : "down") : "down",
        g_config.mqttBroker().c_str(),
        g_config.mqttPort(),
        _uart ? (_uart->isReady() ? 1 : 0) : 0,
        _adc ? (_adc->isReady() ? 1 : 0) : 0,
        _adc ? _adc->adcSource() : "none",
        _gpio ? (_gpio->isReady() ? 1 : 0) : 0,
        _led ? (_led->isReady() ? 1 : 0) : 0,
        _led ? _led->modeStr() : "off",
        _pwm ? (_pwm->isActive() ? 1 : 0) : 0,
        _pwm ? _pwm->pin() : -1,
        _pwm ? _pwm->periodUs() : 0,
        _pwm ? _pwm->dutyPct() : 0,
        _uart ? _uart->dropCount() : 0,
        _adc ? _adc->dropCount() : 0);

    DebugEvent ev;
    ev.type = DebugEventType::SYSTEM;
    ev.timestamp = millis();
    size_t len = (size_t)n < sizeof(ev.data) ? (size_t)n : sizeof(ev.data) - 1;
    memcpy(ev.data, buf, len);
    ev.data[len] = '\0';
    ev.length = (uint16_t)len;
    g_eventBus.push(ev);
}

void DebugGateway::publishError(const char* msg) {
    char buf[220];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"event\",\"kind\":\"error\",\"msg\":\"%s\"}", msg);
    DebugEvent ev;
    ev.type = DebugEventType::ERROR;
    ev.timestamp = millis();
    size_t len = (size_t)n < sizeof(ev.data) ? (size_t)n : sizeof(ev.data) - 1;
    memcpy(ev.data, buf, len);
    ev.data[len] = '\0';
    ev.length = (uint16_t)len;
    g_eventBus.push(ev);
}

void DebugGateway::handleJsonCommand(const JsonObjectConst& req) {
    const char* cmd = req["cmd"] | "";
    if (strcmp(cmd, "uart_config") == 0) {
        unsigned long baud = req["baud"] | 115200;
        uint8_t db = req["data_bits"] | 8;
        uint8_t sb = req["stop_bits"] | 1;
        uint8_t par = req["parity"] | 0;
        _uart->configure(baud, db, sb, par);
        UartConfig uc{baud, db, sb, par};
        g_config.setUart(uc); g_config.save();
        LOG_INFO("CMD", "uart_config baud=%lu", baud);
    } else if (strcmp(cmd, "uart_tx") == 0) {
        const char* data = req["data"] | "";
        if (strlen(data) > 0) { _uart->send(String(data)); LOG_INFO("CMD", "uart_tx %uB", (unsigned)strlen(data)); }
    } else if (strcmp(cmd, "gpio_set") == 0) {
        uint8_t pin = req["gpio"] | 0;
        uint8_t val = req["value"] | 0;
        if (!_gpio->setPin(pin, val)) publishError("gpio_set rejected");
    } else if (strcmp(cmd, "ws2812_set") == 0) {
        const char* mode = req["mode"] | "off";
        Ws2812Mode m = Ws2812Mode::OFF;
        if (!strcmp(mode, "r"))      m = Ws2812Mode::BLINK_R;
        else if (!strcmp(mode, "g")) m = Ws2812Mode::BLINK_G;
        else if (!strcmp(mode, "b")) m = Ws2812Mode::BLINK_B;
        else if (!strcmp(mode, "cycle")) m = Ws2812Mode::CYCLE_RGB;
        _led->setMode(m);
        LOG_INFO("CMD", "ws2812_set mode=%s", _led->modeStr());
    } else if (strcmp(cmd, "pwm_set") == 0) {
        bool active = req["active"] | true;
        uint8_t duty = (uint8_t)(req["duty"] | 0);
        if (!active || duty == 0) {
            _pwm->stop();
            LOG_INFO("CMD", "pwm_set stop");
        } else {
            uint8_t pin = (uint8_t)(req["pin"] | 0);
            uint32_t period = (uint32_t)(req["period"] | 1000);
            if (!_pwm->configure(pin, period, duty))
                publishError("pwm_set rejected: pin in use");
            else
                LOG_INFO("CMD", "pwm_set pin=%u period=%uus duty=%u%%", pin, period, duty);
        }
    } else if (strcmp(cmd, "adc_read") == 0) {
        for (uint8_t ch = 0; ch < 4; ++ch) {
            uint32_t raw = 0; float pre = 0;
            if (_adc->adc()->readChannel(ch, raw, pre)) {
                float v = pre * g_config.adcDivider(ch) + g_config.adcOffset(ch);
                DebugEvent ev;
                ev.type = DebugEventType::ADC_SAMPLE;
                ev.timestamp = millis();
                ev.channel = ch; ev.raw = (uint16_t)raw; ev.voltage = v;
                g_eventBus.push(ev);
            }
        }
        LOG_INFO("CMD", "adc_read");
    } else if (strcmp(cmd, "adc_config") == 0) {
        float fsr = req["fsr"] | 6.144f;
        float div[4] = { req["d0"] | 1.0f, req["d1"] | 1.0f, req["d2"] | 1.0f, req["d3"] | 1.0f };
        float off[4] = {0,0,0,0};
        _adc->configure(fsr, div, off);
        g_config.setAdc(fsr, div, off); g_config.save();
        LOG_INFO("CMD", "adc_config fsr=%.3f", fsr);
    } else if (strcmp(cmd, "wifi_config") == 0) {
        String ssid = req["ssid"] | "";
        String pass = req["pass"] | "";
        g_config.setWifi(ssid, pass); g_config.save();
        _wifi->applySta(ssid, pass);
        LOG_INFO("CMD", "wifi_config");
    } else if (strcmp(cmd, "mqtt_config") == 0) {
        String broker = req["broker"] | "";
        int port = req["port"] | 1883;
        String user = req["user"] | "";
        String pass = req["pass"] | "";
        int keep = req["keep"] | 30;
        bool tls = req["tls"] | false;
        g_config.setMqtt(broker, port, user, pass, keep, tls); g_config.save();
        _mqtt->reconnect();
        LOG_INFO("CMD", "mqtt_config");
    } else if (strcmp(cmd, "device_reset") == 0) {
        LOG_INFO("CMD", "device_reset");
        delay(100);
        ESP.restart();
    } else {
        char m[64];
        snprintf(m, sizeof(m), "unknown cmd: %s", cmd);
        publishError(m);
        LOG_WARN("CMD", "unknown cmd '%s'", cmd);
    }
}

} // namespace RHD
