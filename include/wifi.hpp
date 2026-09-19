#pragma once

#include <cstdint>

// Non-blocking Wi-Fi connection manager.
//
// The device boots and serves regardless of Wi-Fi state; call wifi_service()
// periodically from the main loop and it (re)connects in the background with
// exponential backoff. All functions must be called with the lwIP lock held
// (LwipGuard), matching the rest of the app: the cyw43/lwIP callbacks run
// concurrently in IRQ context.

// Begin managing the station connection. Does not block: the first attempt
// happens on the next wifi_service() call.
void wifi_start(const char* ssid, const char* password, uint32_t auth);

// Advance the connection state machine. Cheap on the common path.
void wifi_service();

// True once the link is up and an IP address has been assigned.
bool wifi_connected();

// Drive the on-board wireless LED as a connection indicator: blink at ~2 Hz
// while not connected, off when connected. Call once per frame.
void wifi_status_led_update();
