#include "wifi.hpp"

#include "config.hpp"

#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

namespace {

enum class WifiState {
    Disconnected,
    Connecting,
    Up
};

WifiState s_state = WifiState::Disconnected;
const char* s_ssid = nullptr;
const char* s_password = nullptr;
uint32_t s_auth = 0;

absolute_time_t s_nextAttempt; // when to (re)connect while Disconnected
absolute_time_t s_deadline;    // give up the current attempt after this
uint32_t s_backoffMs = wifiReconnectMinDelayMs;

uint32_t nextBackoff() {
    uint32_t next = s_backoffMs * 2;
    return next > wifiReconnectMaxDelayMs ? wifiReconnectMaxDelayMs : next;
}

// lwIP's own netif callback restarts mDNS on link/IP changes
// (LWIP_NETIF_EXT_STATUS_CALLBACK); this is a belt-and-suspenders restart on
// the reconnect edge.
void restartMdns() {
    mdns_resp_restart(&cyw43_state.netif[CYW43_ITF_STA]);
}

} // namespace

void wifi_start(const char* ssid, const char* password, uint32_t auth) {
    s_ssid = ssid;
    s_password = password;
    s_auth = auth;
    s_state = WifiState::Disconnected;
    s_backoffMs = wifiReconnectMinDelayMs;
    s_nextAttempt = get_absolute_time(); // connect on the first service call
}

void wifi_service() {
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    // The chip can rejoin on its own; adopt that instead of re-issuing a join.
    if (status == CYW43_LINK_UP && s_state != WifiState::Up) {
        s_state = WifiState::Up;
        s_backoffMs = wifiReconnectMinDelayMs;
        restartMdns();
        return;
    }

    switch (s_state) {
        case WifiState::Disconnected:
            if (time_reached(s_nextAttempt)) {
                if (cyw43_arch_wifi_connect_async(s_ssid, s_password, s_auth) == 0) {
                    s_state = WifiState::Connecting;
                    s_deadline = make_timeout_time_ms(wifiConnectTimeoutMs);
                } else {
                    // Couldn't even queue the join; back off and retry.
                    s_nextAttempt = make_timeout_time_ms(s_backoffMs);
                    s_backoffMs = nextBackoff();
                }
            }
            break;

        case WifiState::Connecting:
            if (time_reached(s_deadline) || status == CYW43_LINK_FAIL ||
                status == CYW43_LINK_BADAUTH || status == CYW43_LINK_NONET) {
                s_state = WifiState::Disconnected;
                s_nextAttempt = make_timeout_time_ms(s_backoffMs);
                s_backoffMs = nextBackoff();
            }
            break;

        case WifiState::Up:
            if (status != CYW43_LINK_UP) {
                // Link dropped or IP lost: reconnect promptly, then back off.
                s_state = WifiState::Disconnected;
                s_nextAttempt = make_timeout_time_ms(wifiReconnectMinDelayMs);
            }
            break;
    }
}

bool wifi_connected() {
    return s_state == WifiState::Up;
}

void wifi_status_led_update() {
    bool on = false;
    if (!wifi_connected()) {
        uint32_t ms = to_ms_since_boot(get_absolute_time());
        on = (ms / wifiLedBlinkHalfPeriodMs) & 1u;
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}
