// Hộp chọn đoạn: sóng âm, kéo đầu / cuối, nghe thử rồi mới lưu
import { useEffect, useRef, useState } from 'react';
import { RATE, prepare, playPreview, soundBounds } from './audio.js';

const MIN = RATE * 0.1;
const fmt = s => s.toFixed(1).replace('.', ',') + ' s';

export default function Trim({ id, name, pcm, room, onDone }) {
  const [range, setRange] = useState(() => {
    // mặc định: từ chỗ bắt đầu có tiếng; dài quá 20 giây thì lấy 10 giây đầu
    let [a, b] = soundBounds(pcm);
    if (b - a < MIN) [a, b] = [0, pcm.length];
    if (b - a > RATE * 20) b = a + RATE * 10;
    return [a, Math.min(pcm.length, Math.max(b, a + MIN))];
  });
  const [stop, setStop] = useState(null);
  const [err, setErr] = useState('');
  const canvas = useRef(null), dlg = useRef(null);
  const [a, b] = range;
  const bytes = (b - a) * 2;

  useEffect(() => { dlg.current.showModal(); }, []);
  useEffect(() => () => stop?.(), [stop]);

  useEffect(() => {
    const cv = canvas.current, dpr = devicePixelRatio || 1;
    const w = (cv.width = cv.clientWidth * dpr), h = (cv.height = cv.clientHeight * dpr);
    const g = cv.getContext('2d'), per = pcm.length / w, css = getComputedStyle(cv);
    const peaks = Float32Array.from({ length: w }, (_, x) => {
      let m = 0;
      for (let j = Math.floor(x * per), e = Math.floor((x + 1) * per); j < e; j++) m = Math.max(m, Math.abs(pcm[j]));
      return m;
    });
    const top = Math.max(...peaks) || 1;
    for (let x = 0; x < w; x++) {
      const bh = Math.max(dpr, (peaks[x] / top) * h), s = x * per;
      g.fillStyle = css.getPropertyValue(s >= a && s <= b ? '--acc' : '--line-strong');
      g.fillRect(x, (h - bh) / 2, 1, bh);
    }
  }, [pcm, a, b]);

  const halt = () => { stop?.(); setStop(null); };
  const set = (i, v) => {
    halt();
    v = Math.round(v * RATE);
    setRange(i ? [a, Math.min(pcm.length, Math.max(v, a + MIN))] : [Math.max(0, Math.min(v, b - MIN)), b]);
  };
  const seg = () => {
    const s = prepare(pcm.subarray(a, b));
    if (!s) setErr('Đoạn này gần như không có tiếng, chọn chỗ khác');
    return s;
  };
  const preview = () => {
    if (stop) return halt();
    const s = seg();
    if (s) setStop(() => playPreview(s, () => setStop(null)));
  };
  const save = () => { const s = seg(); if (s) { halt(); onDone(s); } };
  const close = () => { halt(); onDone(null); };
  const over = bytes > room, len = pcm.length / RATE;

  return (
    <dialog ref={dlg} onCancel={e => { e.preventDefault(); close(); }}>
      <h2>{id < 0 ? 'Chọn đoạn cho âm mới' : `Chọn đoạn thay âm ${id + 1}`}</h2>
      <p className="mut name">{name}</p>
      <canvas className="wave" ref={canvas} />
      {[['Từ', a], ['Đến', b]].map(([label, v], i) => (
        <label key={label} className="row">
          <span>{label}</span>
          <input type="range" min="0" max={len} step="0.1" value={v / RATE} onChange={e => set(i, +e.target.value)} />
          <b>{fmt(v / RATE)}</b>
        </label>
      ))}
      <div className="row">
        <button onClick={preview}>{stop ? '■ Dừng' : '▶ Nghe thử'}</button>
        <span className="mut">Dài {fmt((b - a) / RATE)} · {Math.round(bytes / 1024)} KB</span>
      </div>
      {(over || err) && <p className="err">{over ? 'Dài quá, bộ nhớ không đủ chỗ' : err}</p>}
      <div className="foot">
        <button onClick={close}>Huỷ</button>
        <button className="pri" disabled={over} onClick={save}>{id < 0 ? 'Thêm vào SoundPad' : `Thay âm ${id + 1}`}</button>
      </div>
    </dialog>
  );
}
