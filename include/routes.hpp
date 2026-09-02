#pragma once

#include "api_server.hpp"
#include "shared_data.hpp"

// Registers all HTTP endpoints on the server. Handlers run in lwIP IRQ
// context; they only touch m_shared under LwipGuard.
class Routes {
public:
    explicit Routes(SharedData& shared) : m_shared(shared) {}

    void registerEndpoints(ApiServer& server);

private:
    SharedData& m_shared;
};
