/**
 * @file net_service.cpp
 * @brief WiFi station: credentials, scanning, association, and the state the
 *        Settings page renders.  See the header for scope and threading.
 *
 * WHY THE CREDENTIALS ARE OURS AND NOT THE DRIVER'S
 * -------------------------------------------------
 * esp_wifi can persist its own config in its own NVS namespace.  Two owners of
 * one secret is one owner too many, so the driver is put in WIFI_STORAGE_RAM
 * and this file is the only thing that writes credentials - under the "wifi"
 * namespace, where the Settings page's "Forget" can actually reach them.
 *
 * WHY THE EVENT HANDLERS ARE WHERE THE STATE CHANGES
 * --------------------------------------------------
 * The association result arrives on the esp_event task, not on the caller's.
 * So net_wifi_connect() only *asks* and immediately reports "connecting"; every
 * terminal state is published from a handler.  That keeps one writer per fact,
 * and it is why a caller can never observe "connected" before the stack is.
 */
#include "net_service.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "net";

namespace services {
namespace {

/* ---- what the stack is doing ---------------------------------------------- */

/* Bounded retry.  An access point that reboots or a DHCP server that is slow
 * should not need someone standing at the touch panel, but an unlimited retry
 * loop against a wrong password would just spin. */
constexpr int kMaxRetries   = 3;
constexpr int kRetryDelayMs = 5000;

/* NVS.  Namespace and keys are part of the on-device format: renaming either
 * silently orphans whatever is already stored. */
constexpr const char *kNvsNamespace = "wifi";
constexpr const char *kNvsKeySsid   = "ssid";
constexpr const char *kNvsKeyPass   = "pass";

/* ---- the published snapshot ---------------------------------------------- */

/**
 * @brief The three values a reader wants together.
 *
 * Published as a unit so a caller cannot render the SSID of one connection
 * with the IP of another.  Two buffers, and an atomic index that says which
 * one is live: a writer fills the *other* one and then flips the index, so a
 * reader never sees a partly written string.
 *
 * The residual case is a publish that lands inside a reader's copy and then
 * wraps back around to the same slot.  It needs ~2 publishes within the
 * microseconds a 33-byte copy takes, and the worst outcome is one stale label
 * for one refresh.  Closing it properly needs reader reference counts, which
 * would cost more than the fault.
 */
struct Snapshot {
    WifiState state;
    char      ssid[kWifiSsidMax];
    char      ip[16];
    int8_t    rssi;
};

Snapshot              s_snap[2];
std::atomic<uint32_t> s_live{0};
std::atomic<uint32_t> s_gen{0};

/* Authoritative cursor.  Only ever touched under s_write_lock, so it is safe
 * to read-modify-write it from both the event task and a UI callback. */
Snapshot        s_cur;
SemaphoreHandle_t s_write_lock = nullptr;

void publish_locked()
{
    const uint32_t next = s_live.load(std::memory_order_relaxed) ^ 1u;
    s_snap[next] = s_cur;
    s_live.store(next, std::memory_order_release);
    s_gen.fetch_add(1, std::memory_order_release);
}

/**
 * @brief Update the snapshot and publish it.
 *
 * NULL leaves that string alone; @p rssi defaults to "not associated" because
 * every transition except IP_EVENT_STA_GOT_IP is a step away from a link.
 */
void publish(WifiState st, const char *ssid, const char *ip, int8_t rssi = 0)
{
    if (s_write_lock != nullptr && xSemaphoreTake(s_write_lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_cur.state = st;
    s_cur.rssi  = rssi;
    if (ssid != nullptr) {
        snprintf(s_cur.ssid, sizeof(s_cur.ssid), "%s", ssid);
    }
    if (ip != nullptr) {
        snprintf(s_cur.ip, sizeof(s_cur.ip), "%s", ip);
    }
    publish_locked();
    if (s_write_lock != nullptr) {
        xSemaphoreGive(s_write_lock);
    }
}

const Snapshot &current()
{
    return s_snap[s_live.load(std::memory_order_acquire)];
}

/* ---- scan results --------------------------------------------------------- */

WifiAp   s_aps[kWifiMaxAps] = {};
size_t   s_ap_count = 0;
std::atomic<bool> s_scanning{false};

/* The driver's own records are far larger than what the page needs, so they
 * are converted into s_aps and dropped.  PSRAM: scanning is not latency
 * critical and internal SRAM is the resource the RGB panel competes for. */
wifi_ap_record_t *s_records = nullptr;

esp_timer_handle_t s_retry_timer = nullptr;
int                s_retry = 0;
bool               s_started = false;

/**
 * @brief "The link should be up."  The one flag the retry machinery obeys.
 *
 * Set by connect/reconnect, cleared by disconnect/forget and by giving up.
 * Without it the retry decision has to be inferred from the disconnect reason,
 * which does not work: esp_wifi_disconnect() reports
 * WIFI_REASON_ASSOC_LEAVE (8) when the station was associated and
 * WIFI_REASON_STA_LEAVING (36) when it was not, so a reason allow-list is
 * always one case short.  A stray retry then re-arms *after* forget() has
 * erased the credentials, and the device starts associating with a network
 * nobody asked for.
 *
 * The ordering that makes this work: the flag is cleared synchronously, before
 * esp_wifi_disconnect(), so any event that arrives afterwards - whenever the
 * driver gets round to it - sees the new intent.
 *
 * This is not the same question on_disconnected() asks when it screens out
 * reasons 8 and 36: that one is "was this report a fault", and it has to be
 * answered from the reason, because a call site can leave the link on purpose
 * while still wanting a link.
 */
std::atomic<bool> s_want_connected{false};

/**
 * @brief True between arming a retry and that retry actually going out.
 *
 * Insurance, not the main path.  Two failure reports arriving inside one retry
 * window must not consume two slots from the budget: the second would call
 * esp_timer_start_once() on a timer that is still running, which answers
 * ESP_ERR_INVALID_STATE and does *not* restart it - so the slot would be spent
 * and no extra attempt made, which is worse than either outcome alone.  The
 * window is one retry delay wide, so this needs two genuine failures inside it;
 * it exists because the failure mode is silent, not because it is likely.
 */
std::atomic<bool> s_retry_pending{false};

/** @brief The reason codes worth naming; everything else prints as a number. */
const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_ASSOC_LEAVE:           return "left on purpose (associated)";
    case WIFI_REASON_STA_LEAVING:           return "left on purpose (not associated)";
    case WIFI_REASON_NO_AP_FOUND:           return "no such network";
    case WIFI_REASON_AUTH_FAIL:             return "wrong password";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:return "wrong password (handshake)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "handshake timed out";
    case WIFI_REASON_BEACON_TIMEOUT:        return "lost the access point";
    case WIFI_REASON_ASSOC_FAIL:            return "association refused";
    default:                                return "?";
    }
}

/* ---- NVS ----------------------------------------------------------------- */

bool load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = ssid_size;
    const esp_err_t err = nvs_get_str(h, kNvsKeySsid, ssid, &n);
    if (err == ESP_OK) {
        size_t n_pass = pass_size;
        /* An absent password is a real state (open network), not an error. */
        if (nvs_get_str(h, kNvsKeyPass, pass, &n_pass) != ESP_OK) {
            pass[0] = '\0';
        }
    }
    nvs_close(h);
    return err == ESP_OK && ssid[0] != '\0';
}

esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, kNvsKeySsid, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, kNvsKeyPass, (password != nullptr) ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* ---- retry --------------------------------------------------------------- */

/**
 * @brief Hand the driver the credentials, then start associating.
 *
 * The only place the station config is written, and it has to run on *every*
 * association - including the one at boot - because the config does not
 * survive a reset: WIFI_STORAGE_RAM keeps it in RAM, and the driver's own NVS
 * namespace is disabled so that our copy under "wifi" is the only one.
 *
 * Getting this wrong is silent.  A bare esp_wifi_connect() with no config
 * returns ESP_ERR_WIFI_SSID, raises no event, and leaves the published state
 * on "connecting" for ever - the panel says "connecting", nothing is
 * connecting, and no handler will ever fire to correct it.  Measured on
 * hardware: the boot-time association produced no auth attempt at all in 18 s,
 * while the same credentials applied through this function went from
 * esp_wifi_connect() to "state: init -> auth" in 1.24 s.
 */
esp_err_t apply_and_connect(const char *ssid, const char *password)
{
    wifi_config_t wc = {};
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s",
             (password != nullptr) ? password : "");
    /* No minimum security is asserted: an open AP is a legitimate choice, and
     * the scan list already shows which entries need a password. */
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;

    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err != ESP_OK) {
        return err;
    }
    return esp_wifi_connect();
}

void retry_cb(void *)
{
    /* Whatever the next branch decides, the window this flag describes is over
     * by the time the callback runs. */
    s_retry_pending.store(false, std::memory_order_release);
    if (!s_want_connected.load(std::memory_order_acquire) || !s_started) {
        /* disconnect()/forget() happened while this was pending. */
        return;
    }
    ESP_LOGI(TAG, "attempt %d/%d", s_retry, kMaxRetries);
    esp_wifi_connect();
}

/* ---- events -------------------------------------------------------------- */

void sort_by_strength()
{
    /* Insertion sort: 16 entries at most, and it keeps the "equal RSSI" order
     * stable, which a qsort would not. */
    for (size_t i = 1; i < s_ap_count; ++i) {
        const WifiAp key = s_aps[i];
        size_t j = i;
        while (j > 0 && s_aps[j - 1].rssi < key.rssi) {
            s_aps[j] = s_aps[j - 1];
            --j;
        }
        s_aps[j] = key;
    }
}

void on_scan_done()
{
    uint16_t found = 0;
    /* These two calls are the driver handing its buffer over; skipping the
     * second leaks the scan result until the next scan. */
    esp_wifi_scan_get_ap_num(&found);
    uint16_t take = (found > kWifiMaxAps) ? (uint16_t)kWifiMaxAps : found;
    if (take > 0) {
        if (s_records == nullptr) {
            take = 0;
        } else {
            esp_wifi_scan_get_ap_records(&take, s_records);
        }
    }

    s_ap_count = take;
    for (uint16_t i = 0; i < take; ++i) {
        snprintf(s_aps[i].ssid, sizeof(s_aps[i].ssid), "%s", (const char *)s_records[i].ssid);
        s_aps[i].rssi   = s_records[i].rssi;
        s_aps[i].secure = (s_records[i].authmode != WIFI_AUTH_OPEN);
    }
    sort_by_strength();
    s_scanning.store(false, std::memory_order_release);
    ESP_LOGI(TAG, "scan done: %u found, %u kept, strongest %d dBm", (unsigned)found,
             (unsigned)s_ap_count, s_ap_count ? (int)s_aps[0].rssi : 0);

    /* A scan aborts an association that is in flight, and the driver does not
     * reliably raise a disconnect event for it - so scanning while connecting
     * (which is exactly what the setup overlay does if it is opened during
     * boot-time association) can leave the station reporting "connecting" with
     * nothing actually trying.  Nothing else would ever clear it, because the
     * retry timer was not armed either.   Seen in the probe log, not inferred. */
    if (s_want_connected.load(std::memory_order_acquire) &&
        current().state == WifiState::Connecting) {
        ESP_LOGI(TAG, "re-issuing the association the scan interrupted");
        esp_wifi_connect();
    }
}

void on_disconnected(const wifi_event_sta_disconnected_t *d)
{
    /* A local leave is not a failure, and it is never anything else.
     *
     * WIFI_REASON_ASSOC_LEAVE (8) and WIFI_REASON_STA_LEAVING (36) are raised
     * only by our own esp_wifi_disconnect(), and three call sites use that call
     * as a step *towards* a link rather than away from one: net_wifi_connect()
     * drops the old association before reconfiguring, net_scan_start() clears
     * the radio before scanning, and net_wifi_disconnect() does it on purpose.
     * All three have to survive the event their own call raises.
     *
     * Testing s_want_connected is not enough, which is what the probe log
     * showed: the scan-pause disconnect arrives while the intent is still
     * "we want a link", so it walked into the failure path below, consumed
     * retry slot 1, and left the next real attempt logged as "attempt 2/3" with
     * no "attempt 1/3" - two retries issued where the counter claimed three.
     * Intent and reason answer different questions: intent says whether a link
     * is wanted, this says whether the driver's report describes a fault. */
    const bool deliberate = (d->reason == WIFI_REASON_ASSOC_LEAVE) ||
                            (d->reason == WIFI_REASON_STA_LEAVING);

    /* Second case is the same shape - someone already published the state that
     * follows and this event is only the driver catching up.  Returning without
     * touching anything is what keeps "not configured" from being overwritten
     * by "idle" after a forget(). */
    if (deliberate || !s_want_connected.load(std::memory_order_acquire)) {
        ESP_LOGI(TAG, "link down (reason %u, %s); nothing to do", (unsigned)d->reason,
                 reason_text(d->reason));
        return;
    }

    /* The driver repeating a failure already waiting on a retry.  Placed before
     * the publish below so a repeat does not flash "idle" for the length of the
     * retry delay - see s_retry_pending for why a repeat is not free. */
    if (s_retry_pending.load(std::memory_order_acquire)) {
        ESP_LOGI(TAG, "disconnect %u repeated inside the retry window; not counting it",
                 (unsigned)d->reason);
        return;
    }

    ESP_LOGW(TAG, "disconnected: reason %u (%s)", (unsigned)d->reason, reason_text(d->reason));
    publish(WifiState::Idle, "", "0.0.0.0");

    /* A wrong password is not made right by trying it again; it only turns one
     * failure into four and delays the message the user needs. */
    const bool auth_failed = (d->reason == WIFI_REASON_AUTH_FAIL) ||
                             (d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) ||
                             (d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT);
    if (auth_failed) {
        s_want_connected.store(false, std::memory_order_release);
        publish(WifiState::Error, "", "0.0.0.0");
        ESP_LOGW(TAG, "giving up: the credentials are wrong");
        return;
    }

    if (s_retry >= kMaxRetries) {
        /* Published here rather than five seconds later from retry_cb, so the
         * panel says "error" the moment the last attempt fails. */
        s_want_connected.store(false, std::memory_order_release);
        publish(WifiState::Error, "", "0.0.0.0");
        ESP_LOGW(TAG, "giving up after %d retries", s_retry);
        return;
    }

    ++s_retry;
    /* nullptr for the SSID: it is still the network being attempted, and
     * blanking the row on every retry would make the panel flicker. */
    publish(WifiState::Connecting, nullptr, "0.0.0.0");
    if (s_retry_timer != nullptr) {
        s_retry_pending.store(true, std::memory_order_release);
        esp_timer_start_once(s_retry_timer, (uint64_t)kRetryDelayMs * 1000ULL);
    } else {
        s_want_connected.store(false, std::memory_order_release);
        publish(WifiState::Error, "", "0.0.0.0");
    }
}

void event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START: {
            char ssid[kWifiSsidMax] = {0};
            char pass[kWifiPassMax] = {0};
            if (load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
                ESP_LOGI(TAG, "associating with the stored network");
                s_retry = 0;
                s_retry_pending.store(false, std::memory_order_release);
                s_want_connected.store(true, std::memory_order_release);
                /* The SSID is published, not left blank: if this association
                 * fails, the Settings page should name the network it failed
                 * on rather than show an empty row. */
                publish(WifiState::Connecting, ssid, "0.0.0.0");
                const esp_err_t err = apply_and_connect(ssid, pass);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "boot-time association: %s", esp_err_to_name(err));
                    s_want_connected.store(false, std::memory_order_release);
                    publish(WifiState::Error, nullptr, "0.0.0.0");
                }
            } else {
                publish(WifiState::NotConfigured, "", "0.0.0.0");
            }
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
            const auto *c = static_cast<const wifi_event_sta_connected_t *>(data);
            char ssid[kWifiSsidMax] = {0};
            const size_t n = (c->ssid_len < sizeof(ssid) - 1) ? c->ssid_len : sizeof(ssid) - 1;
            memcpy(ssid, c->ssid, n);
            ESP_LOGI(TAG, "associated with \"%s\", waiting for DHCP", ssid);
            publish(WifiState::Connecting, ssid, "0.0.0.0");
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            on_disconnected(static_cast<const wifi_event_sta_disconnected_t *>(data));
            break;
        case WIFI_EVENT_SCAN_DONE:
            on_scan_done();
            break;
        default:
            break;
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *e = static_cast<const ip_event_got_ip_t *>(data);
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        wifi_ap_record_t ap = {};
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }
        s_retry = 0;
        ESP_LOGI(TAG, "connected, ip %s, rssi %d dBm", ip, rssi);
        publish(WifiState::Connected, nullptr, ip, (int8_t)rssi);
    }
}

}  // namespace

/* ---- public --------------------------------------------------------------- */

esp_err_t net_init()
{
    if (s_write_lock == nullptr) {
        s_write_lock = xSemaphoreCreateMutex();
    }
    s_cur.state = WifiState::Off;
    s_cur.ssid[0] = '\0';
    snprintf(s_cur.ip, sizeof(s_cur.ip), "%s", "0.0.0.0");

    /* The records buffer first: if this cannot be had, scanning is reported as
     * unavailable rather than failing the whole service. */
    s_records = static_cast<wifi_ap_record_t *>(
        heap_caps_malloc(kWifiMaxAps * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM));

    const uint32_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        publish(WifiState::Error, "", "0.0.0.0");
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
        publish(WifiState::Error, "", "0.0.0.0");
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == nullptr) {
        ESP_LOGE(TAG, "no station netif; is CONFIG_ESP_WIFI_ENABLED set?");
        publish(WifiState::Unsupported, "", "0.0.0.0");
        return ESP_ERR_NOT_SUPPORTED;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        publish(WifiState::Error, "", "0.0.0.0");
        return err;
    }

    /* We own the credentials; the driver must not keep a second copy. */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr, nullptr);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, nullptr, nullptr);

    const esp_timer_create_args_t targs = {
        .callback = retry_cb,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_retry",
        .skip_unhandled_events = false,
    };
    if (esp_timer_create(&targs, &s_retry_timer) != ESP_OK) {
        s_retry_timer = nullptr;   /* no retry, but the service still works */
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        /* Modem sleep is the driver default and adds up to a beacon interval of
         * latency to every frame.  This is a mains-powered desk device, and the
         * Settings page polls the state often enough for the delay to show as a
         * connection that lags the button press. */
        esp_wifi_set_ps(WIFI_PS_NONE);
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "starting the station: %s", esp_err_to_name(err));
        publish(WifiState::Error, "", "0.0.0.0");
        return err;
    }
    s_started = true;

    /* The station is up but nothing is published until STA_START lands, which
     * carries "configured or not" with it. */
    const uint32_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "station up; internal heap %u -> %u B (cost %d B), PSRAM records %s",
             (unsigned)heap_before, (unsigned)heap_after,
             (int)heap_before - (int)heap_after,
             (s_records != nullptr) ? "allocated" : "unavailable");
    return ESP_OK;
}

WifiState net_wifi_state()
{
    return current().state;
}

const char *net_wifi_state_text(WifiState st)
{
    switch (st) {
    case WifiState::Off:           return "off";
    case WifiState::NotConfigured: return "not configured";
    case WifiState::Idle:          return "idle";
    case WifiState::Connecting:    return "connecting";
    case WifiState::Connected:     return "connected";
    case WifiState::Error:         return "error";
    case WifiState::Unsupported:   return "unsupported";
    default:                       return "?";
    }
}

const char *net_ssid()
{
    return current().ssid;
}

const char *net_ip()
{
    return current().ip;
}

int net_rssi()
{
    return current().rssi;
}

uint32_t net_generation()
{
    return s_gen.load(std::memory_order_acquire);
}

bool net_wifi_has_credentials()
{
    char ssid[kWifiSsidMax] = {0};
    char pass[kWifiPassMax] = {0};
    return load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
}

esp_err_t net_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) >= kWifiSsidMax || (password != nullptr && strlen(password) >= kWifiPassMax)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    /* NVS first: a credential that survives the boot is the point, and a
     * failure here must not leave the stack trying a password we cannot read
     * back. */
    esp_err_t err = save_credentials(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storing credentials: %s", esp_err_to_name(err));
        return err;
    }

    s_retry = 0;
    s_retry_pending.store(false, std::memory_order_release);
    /* Intent before action: every handler that looks at this runs later and
     * must already see "we want to be connected". */
    s_want_connected.store(true, std::memory_order_release);
    publish(WifiState::Connecting, ssid, "0.0.0.0");

    /* Drop whatever the station is on first - set_config is rejected while
     * associated.  The event this raises is one of the two "left on purpose"
     * codes, which on_disconnected() answers by doing nothing, so it cannot
     * overwrite the "connecting" state just published.  Not connected is not an
     * error here, so the result is deliberately unchecked. */
    esp_wifi_disconnect();

    err = apply_and_connect(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "applying the credentials and connecting: %s", esp_err_to_name(err));
        s_want_connected.store(false, std::memory_order_release);
        publish(WifiState::Error, nullptr, "0.0.0.0");
    }
    return err;
}

esp_err_t net_wifi_reconnect()
{
    char ssid[kWifiSsidMax] = {0};
    char pass[kWifiPassMax] = {0};
    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        return ESP_ERR_NOT_FOUND;
    }
    /* Goes through net_wifi_connect() rather than straight to esp_wifi_connect()
     * so the config, the published state and the retry counter all take the one
     * path.  The NVS write that costs is idempotent and happens at most once
     * per button press. */
    return net_wifi_connect(ssid, pass);
}

esp_err_t net_wifi_disconnect()
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_retry = 0;
    s_retry_pending.store(false, std::memory_order_release);
    /* Cleared before the driver is touched: the disconnect event this raises
     * arrives asynchronously, and it must find "we no longer want a link" -
     * otherwise it arms a retry that outlives the request. */
    s_want_connected.store(false, std::memory_order_release);
    if (s_retry_timer != nullptr) {
        esp_timer_stop(s_retry_timer);
    }
    const esp_err_t err = esp_wifi_disconnect();
    /* Published here rather than from the handler, because an already-idle
     * station raises no event at all. */
    publish(WifiState::Idle, "", "0.0.0.0");
    ESP_LOGI(TAG, "disconnect requested (%s)", esp_err_to_name(err));
    return err;
}

esp_err_t net_wifi_forget()
{
    net_wifi_disconnect();

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_erase_key(h, kNvsKeySsid);
        nvs_erase_key(h, kNvsKeyPass);
        err = nvs_commit(h);
        nvs_close(h);
    }
    s_ap_count = 0;
    publish(WifiState::NotConfigured, "", "0.0.0.0");
    ESP_LOGI(TAG, "credentials erased");
    return err;
}

esp_err_t net_scan_start()
{
    if (!s_started) {
        ESP_LOGW(TAG, "scan requested before the station was up");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_records == nullptr) {
        ESP_LOGW(TAG, "scan requested with no room for the results");
        return ESP_ERR_NO_MEM;
    }
    if (s_scanning.load(std::memory_order_acquire)) {
        return ESP_OK;   /* already running; not an error */
    }

    wifi_scan_config_t sc = {};
    sc.show_hidden = 1;   /* a hidden SSID is still a network someone may want */

    /* A station that is inside an association attempt cannot scan - the driver
     * answers ESP_ERR_WIFI_STATE - and running the two concurrently is not
     * safe the other way round either: observed on hardware, a scan issued
     * while associating once returned ESP_ERR_WIFI_STATE and once succeeded and
     * left the association silently dead.  The user asked for a list, so the
     * association gives way.
     *
     * s_want_connected deliberately stays set and the published state stays
     * "connecting": on_scan_done() re-issues the association once the radio is
     * free again, so this is a pause and not a cancellation.  The disconnect
     * event it raises carries reason 8 or 36 and is screened out by
     * on_disconnected() as a local leave.  No delay is inserted between the two
     * calls - the disconnect is accepted synchronously and the scan follows in
     * the same call, which is what the probe log shows. */
    const bool paused = (current().state == WifiState::Connecting);
    if (paused) {
        ESP_LOGI(TAG, "scan requested while associating; pausing the association");
        esp_wifi_disconnect();
    }

    s_scanning.store(true, std::memory_order_release);
    const esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err != ESP_OK) {
        s_scanning.store(false, std::memory_order_release);
        ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
        /* on_scan_done() is the only thing that resumes a paused association
         * and it will never run, so resume here - otherwise the pause becomes
         * the silent hang it was written to avoid. */
        if (paused && s_want_connected.load(std::memory_order_acquire)) {
            ESP_LOGW(TAG, "resuming the association the failed scan paused");
            esp_wifi_connect();
        }
    } else {
        ESP_LOGI(TAG, "scanning");
    }
    return err;
}

bool net_scan_busy()
{
    return s_scanning.load(std::memory_order_acquire);
}

const WifiAp *net_scan_results(size_t *count)
{
    if (count != nullptr) {
        *count = s_ap_count;
    }
    return s_aps;
}

size_t net_scan_copy(WifiAp *out, size_t max)
{
    if (out == nullptr || max == 0) {
        return 0;
    }
    const size_t n = (s_ap_count < max) ? s_ap_count : max;
    memcpy(out, s_aps, n * sizeof(WifiAp));
    return n;
}

int net_rssi_bars(int8_t rssi)
{
    /* -50 dBm and better is full scale; -90 and worse is nothing. */
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

}  // namespace services
