#pragma once
#include "pico/cyw43_arch.h"

class LwipGuard {
public:
    LwipGuard() {
        cyw43_arch_lwip_begin();
    }

    ~LwipGuard() {
        cyw43_arch_lwip_end();
    }

    LwipGuard(const LwipGuard&) = delete;
    LwipGuard& operator=(const LwipGuard&) = delete;
};