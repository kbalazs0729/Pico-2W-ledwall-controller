#include "app.hpp"
#include "mdns.hpp"
#include "api_server.hpp"
#include "lwip_guard.hpp"
#include "animation.hpp"
#include "shared_data.hpp"
#include "routes.hpp"

#include <stdio.h>

SharedData sharedData {};

int main() {
    if (init_hardware() != 0) {
        printf("Hardware initialization failed\n");
        return -1;
    }

    auto server = ApiServer(httpPort);
    Routes routes(sharedData);
    routes.registerEndpoints(server);
    printf("HTTP server started on port %d with IP %s\n", httpPort, ip4addr_ntoa(netif_ip4_addr(netif_list)));

    [[maybe_unused]] auto mdns = MdnsServer(hostname, "ledfal", httpPort);
    printf("mDNS responder started with hostname: %s.local\n", hostname);

    auto animation = make_animation(sharedData.animationType);
    AnimationType activeAnimation = sharedData.animationType;

    uint64_t lastFrameUs = time_us_64();
    absolute_time_t nextFrame = make_timeout_time_us(frameIntervalUs);

    while (true) {
        {
            LwipGuard guard{};
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, sharedData.ledState);
        }

        uint64_t nowUs = time_us_64();
        float dt = (nowUs - lastFrameUs) / 1e6f;
        lastFrameUs = nowUs;

        {
            // Animation step + transpose are both fast (us-scale); the lwIP
            // lock must NOT be held while waiting for the frame to clock
            // out (ms).
            LwipGuard guard{};
            if (sharedData.mode == DisplayMode::Animation) {
                // Hot-swap the driver if an endpoint selected another one.
                if (sharedData.animationType != activeAnimation) {
                    activeAnimation = sharedData.animationType;
                    animation = make_animation(activeAnimation);
                }
                animation->step(dt, sharedData.matrix);
            }
            led_load_frame(sharedData.matrix.columns[0].pixels.data());
        }
        led_flush_frame();

        // Pace to 60 fps against the 1 MHz hardware timer. Fixed increments
        // of nextFrame avoid drift; if a frame ever overruns, sleep_until
        // returns immediately and we simply run late rather than skip.
        sleep_until(nextFrame);
        nextFrame = delayed_by_us(nextFrame, frameIntervalUs);
    }

    return 0;
}
