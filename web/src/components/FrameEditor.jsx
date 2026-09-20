import { useEffect, useRef, useState } from 'react';
import { getMatrix, setMatrix } from '../api.js';
import {
  MATRIX_COLS,
  MATRIX_ROWS,
  decodeBase64,
  encodeBase64,
  blobToImageData,
  imageDataToBlob,
} from '../matrix.js';

export default function FrameEditor({ onPosted }) {
  const canvasRef = useRef(null);
  const drawing = useRef(false);
  const [color, setColor] = useState('#ff0000');
  const [eraser, setEraser] = useState(false);
  const [status, setStatus] = useState('');

  useEffect(() => {
    const ctx = canvasRef.current.getContext('2d');
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, MATRIX_COLS, MATRIX_ROWS);
  }, []);

  function cellAt(e) {
    const r = canvasRef.current.getBoundingClientRect();
    return {
      x: Math.floor(((e.clientX - r.left) / r.width) * MATRIX_COLS),
      y: Math.floor(((e.clientY - r.top) / r.height) * MATRIX_ROWS),
    };
  }

  function paint(e) {
    const { x, y } = cellAt(e);
    if (x < 0 || x >= MATRIX_COLS || y < 0 || y >= MATRIX_ROWS) return;
    const ctx = canvasRef.current.getContext('2d');
    ctx.fillStyle = eraser ? '#000000' : color;
    ctx.fillRect(x, y, 1, 1);
  }

  function clear() {
    const ctx = canvasRef.current.getContext('2d');
    ctx.fillStyle = '#000000';
    ctx.fillRect(0, 0, MATRIX_COLS, MATRIX_ROWS);
  }

  async function send() {
    try {
      const ctx = canvasRef.current.getContext('2d');
      const img = ctx.getImageData(0, 0, MATRIX_COLS, MATRIX_ROWS);
      await setMatrix(encodeBase64(imageDataToBlob(img)));
      setStatus('Frame sent');
      onPosted?.();
    } catch (e) {
      setStatus(`Send failed: ${e.message}`);
    }
  }

  async function load() {
    try {
      const bytes = decodeBase64(await getMatrix());
      canvasRef.current.getContext('2d').putImageData(blobToImageData(bytes), 0, 0);
      setStatus('Frame loaded');
    } catch (e) {
      setStatus(`Load failed: ${e.message}`);
    }
  }

  return (
    <section className="panel">
      <h2>Frame editor</h2>
      <div className="editor-row">
        <canvas
          ref={canvasRef}
          width={MATRIX_COLS}
          height={MATRIX_ROWS}
          className="frame-canvas"
          onPointerDown={(e) => {
            e.currentTarget.setPointerCapture(e.pointerId);
            drawing.current = true;
            paint(e);
          }}
          onPointerMove={(e) => {
            if (drawing.current) paint(e);
          }}
          onPointerUp={() => {
            drawing.current = false;
          }}
          onPointerLeave={() => {
            drawing.current = false;
          }}
        />
        <div className="editor-tools">
          <label>
            Color
            <input
              type="color"
              value={color}
              onChange={(e) => {
                setColor(e.target.value);
                setEraser(false);
              }}
            />
          </label>
          <button
            className={eraser ? 'active' : ''}
            onClick={() => setEraser((v) => !v)}
          >
            Eraser
          </button>
          <button className="secondary" onClick={clear}>
            Clear
          </button>
          <button onClick={send}>Send frame</button>
          <button className="secondary" onClick={load}>
            Load frame
          </button>
          {status && <p className="hint">{status}</p>}
        </div>
      </div>
    </section>
  );
}
