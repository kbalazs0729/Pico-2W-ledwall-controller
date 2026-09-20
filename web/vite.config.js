import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// The app talks to the LED wall directly (http://ledfal.local by default),
// which is allowed by the firmware's CORS header. No dev proxy is used.
export default defineConfig({
  plugins: [react()],
});
