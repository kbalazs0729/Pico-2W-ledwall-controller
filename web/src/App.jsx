import { useCallback, useEffect, useState } from 'react';
import StatusPanel from './components/StatusPanel.jsx';
import Brightness from './components/Brightness.jsx';
import AnimationPicker from './components/AnimationPicker.jsx';
import FrameEditor from './components/FrameEditor.jsx';
import PreviewPanel from './components/PreviewPanel.jsx';
import { getBrightness, getDeviceUrl, getState, setDeviceUrl } from './api.js';

export default function App() {
  const [device, setDevice] = useState(getDeviceUrl());
  const [connected, setConnected] = useState(false);
  const [mode, setMode] = useState('animation');
  const [animationId, setAnimationId] = useState(0);
  const [brightness, setBrightness] = useState(0);

  const refresh = useCallback(async () => {
    try {
      const [state, bright] = await Promise.all([getState(), getBrightness()]);
      setMode(state.mode);
      setAnimationId(state.animationId);
      setBrightness(bright.brightness);
      setConnected(true);
    } catch {
      setConnected(false);
    }
  }, []);

  useEffect(() => {
    refresh();
    const timer = window.setInterval(refresh, 1000);
    return () => clearInterval(timer);
  }, [refresh]);

  function applyDevice() {
    setDeviceUrl(device);
    refresh();
  }

  return (
    <div className="app">
      <header>
        <h1>LED Wall Control</h1>
        <div className="device">
          <input
            value={device}
            spellCheck={false}
            onChange={(e) => setDevice(e.target.value)}
            onBlur={applyDevice}
            onKeyDown={(e) => {
              if (e.key === 'Enter') applyDevice();
            }}
          />
          <button onClick={applyDevice}>Connect</button>
        </div>
      </header>

      <main>
        <div className="col">
          <StatusPanel connected={connected} mode={mode} animationId={animationId} />
          <Brightness value={brightness} onChanged={refresh} />
          <AnimationPicker activeId={animationId} mode={mode} onChanged={refresh} />
        </div>
        <div className="col">
          <PreviewPanel />
        </div>
        <div className="col">
          <FrameEditor onPosted={refresh} />
        </div>
      </main>
    </div>
  );
}
