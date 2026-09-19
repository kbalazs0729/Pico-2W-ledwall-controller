# LED matrix image blob format

The base64 string encodes an RGB image of `width` × `height` pixels, laid out
**column-major**:

```
Columns { Rows { uint8_t * 3 } }
```

- `width` = number of columns
- `height` = number of rows
- each pixel is three bytes: `R, G, B`
- total decoded size is exactly `width * height * 3` bytes

## Byte order

For each column `col` in `0..width`, then for each row `row` in `0..height`,
emit three bytes `R, G, B`:

```
for col in 0..width:
    for row in 0..height:
        emit R, G, B of pixel(col, row)
```

## Encoding steps

1. Build the `width * height * 3` byte buffer in the column-major order above.
2. Base64-encode it with the **standard** alphabet (`A-Z a-z 0-9 + /`).
3. No line breaks, keep the trailing `=` padding.
4. Send the resulting string as the frame.

## Example

A 3×2 image (3 columns, 2 rows):

| (col,row) | R | G | B |
|-----------|---|---|---|
| (0,0)     | 0 | 0 | 0 |
| (0,1)     | 0 | 1 | 0 |
| (1,0)     | 1 | 0 | 0 |
| (1,1)     | 1 | 1 | 0 |
| (2,0)     | 2 | 0 | 0 |
| (2,1)     | 2 | 1 | 0 |

Raw bytes:

```
00 00 00 00 01 00 01 00 00 01 01 00 02 00 00 02 01 00
```

Base64:

```
AAAAAAEAAQAAAQEAAgAAAgEA
```

## JS reference (browser)

```js
function encodeFrame(width, height, pixels) {
  // pixels: function or callback giving [r, g, b] for (col, row), 0-indexed
  const buf = new Uint8Array(width * height * 3);
  let i = 0;
  for (let col = 0; col < width; col++) {
    for (let row = 0; row < height; row++) {
      const [r, g, b] = pixels(col, row);
      buf[i++] = r;
      buf[i++] = g;
      buf[i++] = b;
    }
  }
  return bytesToBase64(buf);
}

function bytesToBase64(bytes) {
  let bin = "";
  for (const byte of bytes) bin += String.fromCharCode(byte);
  return btoa(bin);
}
```

`btoa` handles the standard alphabet + padding; if the byte count is large
enough to blow the 32-bit string limit, use a chunked approach instead.

## Notes

- The byte order is **column-major** — for each column you walk all rows.
  Do not emit row-major data (row outer loop) or the image will be transposed.
- Orientation: row 0 is the **top** of the image. The firmware compensates
  for strips wired bottom-to-top via `flipVertical` / `flipHorizontal` in
  `include/config.hpp`, so clients never need to rotate the image.
- Pixels are raw RGB, interpreted as sRGB (0–255 per channel).
- Round-trip check: encode, then decode the base64 back and confirm you get the
  exact same `width * height * 3` byte array.