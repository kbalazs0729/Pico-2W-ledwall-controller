import { useEffect, useRef, useState } from 'react';
import { getMatrix } from '../api.js';
import { MATRIX_COLS, MATRIX_ROWS, decodeBase64, blobToImageData } from '../matrix.js';

const INTERVALS = [
  { label: 'Off', value: 0 },
  { label: '0.5 s', value: 500 },
  { label: '1 s', value: 1000 },
  { label: '2 s', value: 2000 },
  { label: '5 s', value: 5000 },
];

export default function PreviewPanel() {
  const canvasRef = useRef(null);
  const [interval, setIntervalMs] = useState(() =>
    Number(localStorage.getItem('previewInterval') ?? 1000),
  );
  const [error, setError] = useState('');

  useEffect(() => {
    localStorage.setItem('previewInterval', String(interval));
    if (!interval) return undefined;

    let cancelled = false;
    async function tick() {
      try {
        const bytes = decodeBase64(await getMatrix());
        if (cancelled) return;
        canvasRef.current?.getContext('2d').putImageData(blobToImageData(bytes), 0, 0);
        setError('');
      } catch (e) {
        if (!cancelled) setError(e.message);
      }
    }

    tick();
    const timer = window.setInterval(tick, interval);
    return () => {
      cancelled = true;
      clearInterval(timer);
    };
  }, [interval]);

  return (
    <section className="panel">
      <h2>Live preview</h2>
      <label className="preview-interval">
        Refresh
        <select value={interval} onChange={(e) => setIntervalMs(Number(e.target.value))}>
          {INTERVALS.map((o) => (
            <option key={o.value} value={o.value}>
              {o.label}
            </option>
          ))}
        </select>
      </label>
      {error && <p className="error">{error}</p>}
      <canvas
        ref={canvasRef}
        width={MATRIX_COLS}
        height={MATRIX_ROWS}
        className="preview-canvas"
      />
    </section>
  );
}
