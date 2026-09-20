import { ANIMATIONS } from './AnimationPicker.jsx';

export default function StatusPanel({ connected, mode, animationId }) {
  const name =
    mode === 'manual' ? 'Manual frame' : ANIMATIONS[animationId] ?? `#${animationId}`;
  return (
    <section className="panel">
      <h2>Status</h2>
      <p className="status-line">
        <span className={`dot ${connected ? 'ok' : 'bad'}`} />
        {connected ? 'Connected' : 'Disconnected'}
      </p>
      <p>
        Mode: <strong>{mode}</strong>
      </p>
      <p>
        Animation: <strong>{name}</strong>
      </p>
    </section>
  );
}
