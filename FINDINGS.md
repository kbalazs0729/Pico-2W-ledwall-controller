# Matrix endpoint debugging findings

## Confirmed symptoms (tested live against `ledfal.local`)

- `GET /` → works (200, HTML).
- `GET /led` → works (200, `LED ON`/`LED OFF`).
- `GET /matrix` → **hangs**, 0 bytes after 5 s timeout (curl exit 28).
- `POST /matrix` with a correctly-sized payload → returns 400 `Invalid matrix data size`.

## Root causes identified

### 1. `LedData<75, 21>` is `packed` only on `Pixel`

`Pixel` is `__attribute__((packed))` (3 bytes), but `Column` and `LedData`
are **not** packed. `std::array<Pixel, rows>` pads each 3-byte pixel to a
4-byte boundary inside the array, and `std::array<Column, cols>` aligns
each column too.

Actual runtime layout:

```
columns[col].pixels[row]  →  { r, g, b, PAD }   // 4 bytes per pixel
```

So `sizeof(LedData<75,21>)` is **6300 bytes**, not the expected 4725.

Expected (canonical) blob size per `helpers/BASE64_FORMAT.md`:

```
75 rows × 21 cols × 3 bytes = 4725 bytes
```

### 2. Serial test works, endpoints disagree

- Serial test encodes `sizeof(sharedData.matrix)` (6300) → base64 of 6300 bytes.
- `GET /matrix` also encodes `sizeof` (6300) → consistent with serial.
- But the real image format (and `show_image.py` / `BASE64_FORMAT.md`) expects
  4725 bytes (75×21×3), so the blob is wrong by 1575 padding bytes.
- `POST /matrix` compares decoded size (from the *correct* 4725-byte payload)
  against `sizeof(sharedData.matrix)` (6300) → always mismatches → 400.

**Result:** both endpoints use `sizeof(sharedData.matrix)` as the truth, but
that truth is wrong because the struct layout has per-pixel padding.

### 3. `GET /matrix` hang is an lwIP send-buffer overflow

`send_response()` builds one giant `std::string` (~6300-byte body + headers)
and calls `tcp_write()` once. lwIP config:

- `MEM_SIZE = 4000`
- `TCP_SND_BUF = 8 * TCP_MSS = 8 * 1460 = 11680`
- `PBUF_POOL_SIZE = 24`

But the response (~6.4 KB) exceeds the available TX memory in one call,
`tcp_write()` returns `ERR_MEM` (ignored), then `tcp_output()` sends
nothing → client hangs forever. No `tcp_close()` fallback happens because
the code relies on the `on_sent` callback, which never fires.

The small endpoints (`/`, `/led`) work because they fit in the buffer.

## Fix plan

1. **Fix the data layout** so `sizeof(LedData<75,21>) == 4725`:
   - Mark `Column` and `LedData` `packed` too, OR
   - Serialize pixel bytes manually (loop cols×rows, emit 3 bytes each)
     instead of `reinterpret_cast` + `sizeof`.
   - Manual serialization is safer: `reinterpret_cast` over a
     `std::array<Column,cols>` with padding is UB-adjacent.

2. **Fix the size check** in `POST /matrix` to expect `75*21*3 = 4725`.

3. **Fix the GET hang**:
   - Send the response in TCP-MSS-sized chunks, waiting on `on_sent` to
     send the next chunk, OR
   - Increase `MEM_SIZE` in `lwipopts.h` (e.g. 16000) and
     `TCP_SND_BUF`, OR
   - Both: chunked send is the robust fix; bumping MEM_SIZE alone may
     still fail for the full 6.4 KB single write.

4. **Check `tcp_write` return value** and handle `ERR_MEM` by waiting for
   `on_sent` (lwIP poll/sent callback pattern) rather than dropping the
   response.

## Key numbers

| Item | Value |
|------|-------|
| Expected blob bytes | 4725 = 75 × 21 × 3 |
| Actual `sizeof(LedData<75,21>)` | **4725** (see corrections below) |
| lwIP version | 2.2.1 |
| `MEM_SIZE` | 4000 (now 16000) |
| `TCP_MSS` | 1460 |
| arch | `pico_cyw43_arch_lwip_threadsafe_background` |

## Corrections (verified with arm-none-eabi-g++ static_assert)

- **Root cause 1 is wrong.** `Pixel` is `packed`, so it has alignment 1 and
  `std::array` inserts no padding: `sizeof(LedData<75,21>) == 4725` on the
  target toolchain. No layout fix was needed.
- **The POST 400 was caused by request truncation**, not the size constant:
  `handle_recv` used `p->len` (first pbuf only) and never reassembled TCP
  segments, so a 6.3 KB body was routed in pieces. Fixed by accumulating the
  full pbuf chain across segments and waiting for `Content-Length` bytes
  before routing. The handler now also strips a `pixels=` form prefix.
- **The GET hang** was the single ~6.4 KB `tcp_write` (ignored `ERR_MEM`).
  Fixed with chunked sending driven by `on_sent`, plus `MEM_SIZE` bumped to
  16000 for the copy-buffer heap.
- Also fixed: out-of-bounds read in `fromBase64` (indexed the 64-char
  alphabet by arbitrary chars) and a signed-shift UB in the same function.
- **Regression found after the chunked-send fix:** `on_sent`/`on_recv`
  called `tcp_close()` and then freed the per-connection state immediately.
  But `tcp_close()` only moves the pcb to FIN_WAIT/LAST_ACK — it is freed
  later by `tcp_slowtmr`, and ACKs arriving in the meantime still invoke the
  `sent` callback with the (already freed) `callback_arg` → use-after-free,
  heap corruption, device stops answering after the first request.
  Fixed by `close_connection()`: deregister `arg`/`recv`/`sent`/`err`
  callbacks *before* `tcp_close()`, fall back to `tcp_abort()` on failure,
  then free the state.
