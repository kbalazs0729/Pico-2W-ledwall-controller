#pragma once

#include <stdio.h>
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

class MdnsServer {
public:
    MdnsServer(const char* hostname, const char* deviceName);
    ~MdnsServer();
};