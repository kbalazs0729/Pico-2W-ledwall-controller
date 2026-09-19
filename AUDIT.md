# Codebase audit (pre-bus refactor)

Two-pass audit (networking + LED/PIO/DMA), RP2350 / Pico 2 W,
`pico_cyw43_arch_lwip_threadsafe_background` (lwIP callbacks run in IRQ
context, concurrent with the main loop; shared state protected by
`LwipGuard`).

Disposition legend: **[fixed]** done in the audit-fix commit ·
**[doc]** known limitation, documented here, deliberately not fixed now ·
**[asked]** needed a user decision (see bottom) · **[bus]** resolved by the
bus refactor.

---

## 1. Definite bugs

### Networking

1. **[fixed]** `mdns.cpp`: `mdns_resp_init/add_netif/add_service` called from
   the main thread without the lwIP lock (IRQ context runs concurrently).
2. **[fixed]** `api_server.cpp` Content-Length match not line-anchored:
   `find("content-length:")` also matches `X-Content-Length:` or the same
   string inside another header value.
3. **[fixed]** Response body not sliced to Content-Length — pipelined/extra
   bytes would be passed to the handler.
4. **[fixed]** Unbounded request accumulation + no Content-Length validation
   (`Content-Length: -1` → ~4 GB via `strtoul`) → heap-exhaustion DoS.
   Now: header/body caps, validated parse, 400 + close on violation.
5. **[doc]** HTTP pipelining unsupported: a second request in the same
   segment is discarded; a request arriving mid-response corrupts `tx`.
   Mitigation: every response carries `Connection: close`, well-behaved
   clients won't pipeline.
6. **[fixed]** `tcp_listen()` result unchecked (NULL on MEMP exhaustion →
   NULL deref); bind-failure path leaked the pcb.
7. **[doc]** No `tcp_poll`/idle timeout: half-open connections hold pcbs
   forever (`MEMP_NUM_TCP_PCB=12` → ~11 idle conns DoS the server).
   Acceptable on a LAN toy for now; add `tcp_poll` with an idle counter
   (e.g. 4 × 500 ms polls) if it ever bites.
8. **[fixed]** `pump_tx` could hang the connection if the very first
   `tcp_write` failed (nothing in flight → `on_sent` never fires). Now the
   connection is closed on first-write failure.
9. **[fixed]** `mdns.hpp`: `~MdnsServer()` declared, never defined. Removed.
10. **[fixed]** `api_server.cpp`: reason phrase hardcoded `" OK"` for all
    statuses (a 400 said "400 OK").
11. **[fixed]** `tcp_new_ip_type(IPADDR_ANY)` worked only by coincidence
    (0 == `IPADDR_TYPE_V4`); now `IPADDR_TYPE_ANY`.
12. **[fixed]** `mdns.cpp`: TXT item length 7 included the NUL
    ("path=/" is 6) → NUL byte inside the TXT record.
13. **[fixed]** `on_recv` error path (`err != ERR_OK` with pbuf) left the
    connection silently open; now closed.

### LED / system

14. **[fixed]** `main.cpp`: `#include <boards/pico_w.h>` in a `pico2_w`
    build (wrong board header; board headers shouldn't be included directly
    at all). Removed, plus unused `<atomic>`.
15. **[fixed]** `main.cpp`: `huePhase` wrap was single-subtract; a stall
    > 0.5 s left phase denormalized for several frames. Now `fmodf`.
16. **[fixed]** `main.cpp`: `sharedData.ledState` read without `LwipGuard`
    (main loop + `/led` response expression). Both reads now under the guard.
17. **[bus]** `led_data.hpp::getRowData` was dead code with a latent GRB/RGB
    channel-order bug. Removed; the bus transpose replaces it.
18. **[doc]** `ws2812.pio` T0H = 250 ns — exactly the WS2812 spec floor
    (400±150 ns). Canonical pico-examples values; fine on genuine parts,
    marginal for clones. Revisit T1/T2/T3 if strips misbehave; WS2815
    datasheet check still open (PARALLEL_MATRIX_PLAN.md §1).
19. **[fixed]** `api_server.cpp`: `pump_tx`/recv-error paths above; also
    `pio_add_program` return is now checked (silent garbage offset before).

---

## 2. Oversights / code smells

- **[fixed]** `api_server.hpp`: dead `Endpoint::operator==` (std::map uses
  only `operator<`). `Endpoint` still stores borrowed `const char*` keys —
  fine as long as only string literals are registered; noted as a footgun.
- **[fixed]** `base64`: alphabet array lived in the header (one copy per
  TU) → moved to .cpp; `toBase64` takes `const uint8_t*`; dead ternary
  condition removed.
- **[fixed]** `main.cpp`: POST `/matrix` `memcpy` target was `&matrix.columns`
  while the size check used `sizeof(matrix)` — unified to `&matrix`, plus a
  `static_assert(sizeof(matrix) == rows*cols*3)` guarding the packed-layout
  assumption the base64 GET/POST rely on.
- **[fixed]** `CMakeLists.txt`: `pico_lwip_http` linked but unused (server
  is hand-rolled on raw `tcp.h`) → removed; `-Wall -Wextra` added;
  `compile_commands.json` untracked + gitignored (generated file, absolute
  paths, pure diff churn).
- **[doc]** Handlers (base64 encode/decode, `std::string` churn, `new`/
  `delete`) run in cyw43 IRQ context. Safe in this arch mode (lwIP core lock
  is recursive; newlib malloc locking is IRQ-safe), but keep handlers small.
- **[doc]** `LwipGuard` inside HTTP handlers is a no-op (callbacks already
  run with the lwIP core lock held, mutex is recursive). Kept as
  documentation-by-code; the *main-loop* guard is the one doing work.
- **[doc]** `static std::string base64Matrix` in GET /matrix is shared
  across connections — safe only because lwIP serializes all callbacks in
  one context. Do not access it from the main loop.
- **[fixed]** Stale comments corrected: mdns "picow.local" (hostname is
  `ledfal`), "serve from ROM" (`std::string` heap-allocates), rainbow
  "every 4 s" (2.0 cycles/s = 0.5 s/cycle), "halves the intensity"
  (brightness 255), "~5.4 ms at 165 LEDs" (formula comment instead),
  `led_data.hpp` "SPI based LED drivers" (WS2812 is not SPI),
  api_server "another thread" (it's IRQ context).

---

## 3. Asked the user — RESOLVED

1. **Wi-Fi credentials committed to git** → moved to gitignored
   `include/secrets.hpp` (template: `secrets.hpp.example`); git history
   rewritten to purge both networks' creds.
2. **POST /matrix is dead** → mode toggle added: `DisplayMode {Animation,
   Manual}` in `SharedData`; POST /matrix switches to Manual, and
   `POST /animation` with an empty body resumes the current animation
   (`GET /animation` reports mode + id). The old `/animate` endpoint was
   folded into `/animation`.
3. **Button pins** → confirmed **GP14 / GP15**; final layout stays
   bus A = GP0–13 (14 lanes) + bus B = GP16–22 (7 lanes), reserved as
   `buttonPinA/buttonPinB` in `config.hpp`.

## 4. Deliberately not fixed (rationale)

- HTTP pipelining (#5) — `Connection: close` makes it unreachable in
  practice; a real fix means a request queue per connection.
- `tcp_poll` idle reaping (#7) — noted, one callback + counter when needed.
- No `Transfer-Encoding: chunked` support, no `Expect: 100-continue`, no
  query-string stripping — out of scope for this API surface.
- T0H at spec floor (#18) — canonical timing kept; hardware-verified.
