#include "mdns.hpp"

static void srv_txt(struct mdns_service *service, void *txt_userdata) {
    // Add text records here if needed (e.g. path=/index.html)
    mdns_resp_add_service_txtitem(service, "path=/", 7);
}

MdnsServer::MdnsServer(const char* hostname, const char* deviceName) {
    printf("Initializing mDNS responder...\n");
    
    // Get the primary network interface from lwIP
    struct netif *net_interface = &cyw43_state.netif[CYW43_ITF_STA];
    
    // 1. Initialize the global responder
    mdns_resp_init();
    
    // 2. Add our active network interface to mDNS, assigning its hostname and TTL slot
    // This makes the Pico accessible via 'picow.local'
    s8_t slot = mdns_resp_add_netif(net_interface, hostname);
    
    if (slot >= 0) {
        // 3. Optional: Advertise services (e.g., exposing an HTTP Server on Port 80)
        mdns_resp_add_service(net_interface, deviceName, "_http", 
                              DNSSD_PROTO_TCP, 80, srv_txt, NULL);
    } else {
        printf("Failed to register mDNS interface.\n");
    }
}