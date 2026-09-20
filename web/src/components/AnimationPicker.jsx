import { resumeAnimation, setAnimation } from '../api.js';

// Must match AnimationType in include/animation.hpp (append-only ids).
export const ANIMATIONS = [
  'Rainbow',
  'Bad Apple',
  'Fire',
  'Rain',
  'Stars',
  'Plasma',
  'Fire2',
  'RainFill',
  'Tetris',
];

export default function AnimationPicker({ activeId, mode, onChanged }) {
  async function choose(id) {
    try {
      await setAnimation(id);
      onChanged?.();
    } catch {
      /* status panel will show the disconnect */
    }
  }

  async function resume() {
    try {
      await resumeAnimation();
      onChanged?.();
    } catch {
      /* ignore */
    }
  }

  return (
    <section className="panel">
      <h2>Animations</h2>
      <div className="anim-grid">
        {ANIMATIONS.map((name, id) => (
          <button
            key={id}
            className={mode === 'animation' && activeId === id ? 'active' : ''}
            onClick={() => choose(id)}
          >
            <span className="anim-id">{id}</span>
            {name}
          </button>
        ))}
      </div>
      <button className="secondary" onClick={resume}>
        Resume animation
      </button>
    </section>
  );
}
