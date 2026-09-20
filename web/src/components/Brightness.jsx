import { useEffect, useRef, useState } from 'react';
import { setBrightness } from '../api.js';

export default function Brightness({ value, onChanged }) {
  const [local, setLocal] = useState(value);
  const debounce = useRef(null);
  // Don't let the 1 s status poll fight the slider while the user drags it.
  const syncGuard = useRef(0);

  useEffect(() => {
    if (Date.now() > syncGuard.current) setLocal(value);
  }, [value]);

  function change(e) {
    const v = Number(e.target.value);
    setLocal(v);
    syncGuard.current = Date.now() + 1500;
    if (debounce.current) clearTimeout(debounce.current);
    debounce.current = setTimeout(async () => {
      try {
        await setBrightness(v);
        onChanged?.();
      } catch {
        /* ignore */
      }
    }, 150);
  }

  return (
    <section className="panel">
      <h2>Brightness</h2>
      <div className="slider-row">
        <input type="range" min="0" max="255" value={local} onChange={change} />
        <span className="value">{local}</span>
      </div>
    </section>
  );
}
