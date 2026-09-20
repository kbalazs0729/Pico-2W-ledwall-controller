// Matrix geometry must match include/config.hpp (matrixRows / matrixCols).
export const MATRIX_COLS = 21;
export const MATRIX_ROWS = 75;
export const MATRIX_BYTES = MATRIX_COLS * MATRIX_ROWS * 3;

// The /matrix blob is column-major RGB: for each column, each row, R,G,B.
// These helpers convert between that blob and canvas ImageData (row-major
// RGBA, index = (row * COLS + col) * 4).

export function blobToImageData(bytes) {
  if (bytes.length !== MATRIX_BYTES) {
    throw new Error(`expected ${MATRIX_BYTES} bytes, got ${bytes.length}`);
  }
  const img = new ImageData(MATRIX_COLS, MATRIX_ROWS);
  for (let col = 0; col < MATRIX_COLS; col++) {
    for (let row = 0; row < MATRIX_ROWS; row++) {
      const s = (col * MATRIX_ROWS + row) * 3;
      const d = (row * MATRIX_COLS + col) * 4;
      img.data[d] = bytes[s];
      img.data[d + 1] = bytes[s + 1];
      img.data[d + 2] = bytes[s + 2];
      img.data[d + 3] = 255;
    }
  }
  return img;
}

export function imageDataToBlob(imageData) {
  const bytes = new Uint8Array(MATRIX_BYTES);
  let i = 0;
  for (let col = 0; col < MATRIX_COLS; col++) {
    for (let row = 0; row < MATRIX_ROWS; row++) {
      const p = (row * MATRIX_COLS + col) * 4;
      bytes[i++] = imageData.data[p];
      bytes[i++] = imageData.data[p + 1];
      bytes[i++] = imageData.data[p + 2];
    }
  }
  return bytes;
}

export function decodeBase64(b64) {
  const bin = atob(b64.trim());
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}

export function encodeBase64(bytes) {
  let bin = '';
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}
