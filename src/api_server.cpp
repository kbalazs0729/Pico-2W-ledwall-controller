#include "api_server.hpp"

#include <lwip/tcp.h>
#include <lwip/ip4_addr.h>
#include <pico/cyw43_arch.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

ApiServer::ApiServer(int port) {
    this->m_port = port;

    // We are working on another thread, so we need to lock lwIP
    cyw43_arch_lwip_begin();

    // We create a new TCP PCB (Protocol Control Block) for our server
    // that is listening on any IP address (IPADDR_ANY)
    this->m_server_pcb = tcp_new_ip_type(IPADDR_ANY);
    if (this->m_server_pcb == nullptr) {
        // Handle error: unable to create PCB
        cyw43_arch_lwip_end();
        printf("Error: Unable to create new TCP PCB\n");
        return;
    }

    // We bind the PCB to the specified port
    if (tcp_bind(this->m_server_pcb, IP_ANY_TYPE, this->m_port) != ERR_OK) {
        // Handle error: unable to bind PCB
        cyw43_arch_lwip_end();
        printf("Error: Unable to bind TCP PCB to port %d\n", this->m_port);
        return;
    }

    // We set the PCB to listen for incoming connections
    this->m_server_pcb = tcp_listen(this->m_server_pcb);
    tcp_arg(this->m_server_pcb, this);
    tcp_accept(this->m_server_pcb, on_accept);

    // Unlock lwIP after we're done
    cyw43_arch_lwip_end();
}

ApiServer::~ApiServer() {
    // We are working on another thread, so we need to lock lwIP
    cyw43_arch_lwip_begin();

    // Close the server PCB if it exists
    if (this->m_server_pcb != nullptr) {
        tcp_close(this->m_server_pcb);
        this->m_server_pcb = nullptr;
    }

    // Unlock lwIP after we're done
    cyw43_arch_lwip_end();
}

void ApiServer::add_endpoint(const char* path, const Method method, EndpointHandlerFunc handlerPtr) {
    Endpoint endpoint {method, path};
    this->m_endpoints[endpoint] = handlerPtr;
}

/* 
 * C callback for accepting new connections
 * We set the argument to the ApiServer instance so we can access it in the callback
 */
err_t ApiServer::on_accept(void* arg, tcp_pcb* new_pcb, err_t err) {
    if (err != ERR_OK || new_pcb == nullptr) return ERR_VAL;

    auto server = static_cast<ApiServer*>(arg);
    auto state = new ConnectionState{server};
    tcp_arg(new_pcb, state);
    tcp_recv(new_pcb, on_recv);
    tcp_sent(new_pcb, on_sent);
    tcp_err(new_pcb, on_error);

    return ERR_OK;
}

void ApiServer::on_error(void* arg, err_t err) {
    printf("TCP error: %d\n", err);
    // lwIP has already freed the pcb when this callback runs; only drop our state.
    delete static_cast<ConnectionState*>(arg);
}

// C callback for receiving data
// We route the request to the appropriate handler based on the registered endpoints
err_t ApiServer::on_recv(void* arg, tcp_pcb* tpcb, pbuf* p, err_t err) {
    auto state = static_cast<ConnectionState*>(arg);

    // Was the connection closed by the client?
    if (p == nullptr) {
        ApiServer::close_connection(tpcb, state);
        return ERR_OK;
    }

    // Handle the received data
    if (err == ERR_OK) {
        state->server->handle_recv(tpcb, p, *state);
    }

    // Free the pbuf after processing
    pbuf_free(p);
    return ERR_OK;
}

// C callback for when data we sent has been acknowledged by the client.
// Large responses are sent in chunks: as data gets acknowledged, send buffer
// space frees up and we queue the next chunk. The connection is closed only
// once the whole response has been transmitted and acknowledged.
err_t ApiServer::on_sent(void* arg, tcp_pcb* tpcb, u16_t len) {
    auto state = static_cast<ConnectionState*>(arg);

    if (state->tx_offset < state->tx.size()) {
        state->server->pump_tx(tpcb, *state);
    }

    if (state->tx_offset >= state->tx.size()) {
        // Everything queued has been acknowledged; we can close now.
        ApiServer::close_connection(tpcb, state);
    }

    return ERR_OK;
}

void ApiServer::close_connection(tcp_pcb* tpcb, ConnectionState* state) {
    // Deregister all callbacks before closing. After tcp_close() the pcb is
    // not freed immediately: it lingers in FIN_WAIT/LAST_ACK until the FIN
    // handshake completes, and incoming packets during that time would still
    // invoke the sent/recv/err callbacks with our (about to be freed) state,
    // which is a use-after-free.
    tcp_arg(tpcb, nullptr);
    tcp_recv(tpcb, nullptr);
    tcp_sent(tpcb, nullptr);
    tcp_err(tpcb, nullptr);

    if (tcp_close(tpcb) != ERR_OK) {
        // Closing failed (no memory for the FIN segment): abort instead.
        // This frees the pcb immediately and, since the error callback was
        // cleared above, nothing is called back.
        tcp_abort(tpcb);
    }

    delete state;
}

err_t ApiServer::handle_recv(tcp_pcb* tpcb, pbuf* p, ConnectionState& state) {
    // Tell lwIP that we have consumed the received data. This advances the
    // receive window (rcv_wnd); without it, tcp_close() would send a RST
    // (reset) instead of a FIN and the client would see an empty response.
    tcp_recved(tpcb, p->tot_len);

    // Append the whole pbuf chain (tot_len can span multiple pbufs) to the
    // receive buffer. Large requests (e.g. POST /matrix) arrive in several
    // TCP segments, so we must accumulate until the request is complete.
    std::size_t old_size = state.rx.size();
    state.rx.resize(old_size + p->tot_len);
    pbuf_copy_partial(p, state.rx.data() + old_size, p->tot_len, 0);

    // Wait until the full header has arrived
    auto header_end = state.rx.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return ERR_OK;
    }

    // Parse Content-Length (case-insensitive) from the header section
    std::size_t content_length = 0;
    {
        std::string headers = state.rx.substr(0, header_end);
        for (auto& c : headers) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        constexpr const char* key = "content-length:";
        auto pos = headers.find(key);
        if (pos != std::string::npos) {
            content_length = std::strtoul(headers.c_str() + pos + strlen(key), nullptr, 10);
        }
    }

    // Wait until the full body has arrived
    std::size_t body_offset = header_end + 4;
    if (state.rx.size() < body_offset + content_length) {
        return ERR_OK;
    }

    route_request(tpcb, state, body_offset);
    state.rx.clear();

    return ERR_OK;
}

void ApiServer::route_request(tcp_pcb* tpcb, ConnectionState& state, std::size_t body_offset) {
    std::string_view request_view(state.rx);

    // Find the end of the request line (the first line of the HTTP request)
    auto line_end = request_view.find("\r\n");
    if (line_end == std::string_view::npos) {
        send_response(tpcb, state, 400, "text/plain", "400: Bad Request");
        return;
    }

    // Extract the request line (e.g., "GET /path HTTP/1.1")
    std::string_view request_line = request_view.substr(0, line_end);
    auto space1 = request_line.find(' ');
    auto space2 = request_line.rfind(' ');

    if (space1 == std::string_view::npos || space2 == std::string_view::npos || space1 == space2) {
        send_response(tpcb, state, 400, "text/plain", "400: Bad Request");
        return;
    }

    // Extract the method and path from the request line
    std::string_view method_str = request_line.substr(0, space1);
    std::string_view path_str = request_line.substr(space1 + 1, space2 - space1 - 1);

    Method method;
    if (method_str == "GET") {
        method = Method::GET;
    } else if (method_str == "POST") {
        method = Method::POST;
    } else {
        send_response(tpcb, state, 405, "text/plain", "405: Method Not Allowed");
        return;
    }

    // We enforce null-termination for the path string to ensure safe usage
    std::string path(path_str);

    // Create an Endpoint object to look up the handler
    Endpoint endpoint {method, path.c_str()};

    // Look up the handler for the endpoint
    auto it = m_endpoints.find(endpoint);
    // If the endpoint is not found, we send a 404 response
    if (it == m_endpoints.end()) {
        send_response(tpcb, state, 404, "text/plain", "404: Not Found");
        return;
    }

    // The body starts right after the header terminator. handle_recv only
    // calls us once Content-Length bytes are present, so this is complete.
    std::string_view body = request_view.substr(body_offset);

    // Call the handler function for the endpoint
    EndpointHandlerFunc handler = it->second;
    if (handler) {
        auto response = handler(body);
        send_response(tpcb, state, response.status, response.content_type, response.body);
    } else {
        send_response(tpcb, state, 500, "text/plain", "500: Internal Server Error");
    }
}

void ApiServer::send_response(tcp_pcb* tpcb, ConnectionState& state, int status, const char* content_type, const char* body) {
    // We construct the full HTTP response and keep it in the connection
    // state; pump_tx sends it in chunks as send-buffer space allows.
    state.tx.clear();
    state.tx_offset = 0;
    state.tx += "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    state.tx += "Content-Type: " + std::string(content_type) + "\r\n";
    state.tx += "Content-Length: " + std::to_string(strlen(body)) + "\r\n";
    state.tx += "Connection: close\r\n";
    state.tx += "\r\n";
    state.tx += body;

    pump_tx(tpcb, state);
}

void ApiServer::pump_tx(tcp_pcb* tpcb, ConnectionState& state) {
    while (state.tx_offset < state.tx.size()) {
        // Never queue more than the send buffer can take
        u16_t avail = tcp_sndbuf(tpcb);
        if (avail == 0) {
            return; // wait for on_sent to free up buffer space
        }

        std::size_t remaining = state.tx.size() - state.tx_offset;
        u16_t chunk = static_cast<u16_t>(std::min<std::size_t>(remaining, std::min<u16_t>(avail, TCP_MSS)));

        // TCP_WRITE_FLAG_COPY copies the data into lwIP's buffers, so the
        // chunk stays valid even if state.tx is modified later.
        err_t err = tcp_write(tpcb, state.tx.data() + state.tx_offset, chunk, TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) {
            // Out of lwIP memory; the rest is sent from on_sent as
            // previously queued data gets acknowledged and freed.
            break;
        }
        if (err != ERR_OK) {
            printf("tcp_write failed: %d\n", err);
            break;
        }

        state.tx_offset += chunk;
    }

    tcp_output(tpcb);
}
