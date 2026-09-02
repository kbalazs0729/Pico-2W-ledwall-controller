#include "mdns.hpp"
#include "lwip_guard.hpp"

static void srv_txt(struct mdns_service *service, void * /*txt_userdata*/) {
    // Add text records here if needed. NOTE: the length excludes the NUL.
    mdns_resp_add_service_txtitem(service, "path=/", 6);
}

MdnsServer::MdnsServer(const char* hostname, const char* deviceName, uint16_t port) {
    printf("Initializing mDNS responder...\n");

    // mdns_resp_* are lwIP core APIs; lwIP runs concurrently in IRQ context,
    // so these calls must hold the lwIP lock (runs in the main thread here).
    LwipGuard guard{};

    // Get the primary network interface from lwIP
    struct netif *net_interface = &cyw43_state.netif[CYW43_ITF_STA];

    // 1. Initialize the global responder
    mdns_resp_init();

    // 2. Add our active network interface to mDNS, assigning its hostname
    //    and TTL slot. This makes the Pico accessible via '<hostname>.local'
    s8_t slot = mdns_resp_add_netif(net_interface, hostname);

    if (slot >= 0) {
        // 3. Optional: Advertise services (e.g., exposing an HTTP Server)
        err_t err = mdns_resp_add_service(net_interface, deviceName, "_http",
                                          DNSSD_PROTO_TCP, port, srv_txt, NULL);
        if (err != ERR_OK) {
            printf("Failed to advertise mDNS service (err %d).\n", err);
        }
    } else {
        printf("Failed to register mDNS interface.\n");
    }
}
