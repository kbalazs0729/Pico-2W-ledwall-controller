// Thin wrapper around the firmware HTTP API. The device sends
// Access-Control-Allow-Origin, so the browser can call it directly.
//
// POST bodies are plain strings (text/plain), which are "simple" CORS
// requests: no preflight is required.

const STORAGE_KEY = 'deviceUrl';
const DEFAULT_DEVICE_URL = 'http://ledfal.local';

let deviceUrl = localStorage.getItem(STORAGE_KEY) || DEFAULT_DEVICE_URL;

export function getDeviceUrl() {
  return deviceUrl;
}

export function setDeviceUrl(url) {
  deviceUrl = (url || DEFAULT_DEVICE_URL).trim().replace(/\/+$/, '');
  localStorage.setItem(STORAGE_KEY, deviceUrl);
}

async function request(path, options) {
  const res = await fetch(deviceUrl + path, options);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res;
}

export async function getState() {
  return (await request('/animation')).json();
}

export async function setAnimation(id) {
  await request('/animation', { method: 'POST', body: String(id) });
}

export async function resumeAnimation() {
  await request('/animation', { method: 'POST', body: '' });
}

export async function getBrightness() {
  return (await request('/brightness')).json();
}

export async function setBrightness(value) {
  await request('/brightness', { method: 'POST', body: String(value) });
}

export async function getMatrix() {
  return (await request('/matrix')).text();
}

export async function setMatrix(base64) {
  await request('/matrix', { method: 'POST', body: base64 });
}
