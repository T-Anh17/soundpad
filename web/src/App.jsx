import { useEffect, useRef, useState } from 'react';
import { RATE, decodeFile, encodePcm16 } from './audio.js';
import { connect, supported } from './ble.js';
import Trim from './Trim.jsx';

const KEYS = 4;  // trùng KEY_COUNT trong esp/include/config.h
const NEW = -1;  // số hiệu "âm mới" khi nạp
const secs = bytes => (bytes / 2 / RATE).toFixed(1).replace('.', ',') + ' giây';
const mins = bytes => { const s = Math.floor(bytes / 2 / RATE); return s >= 60 ? `${Math.floor(s / 60)} phút ${s % 60} giây` : `${s} giây`; };

// Tên gửi lên board tối đa 60 byte UTF-8 (board cũng tự cắt)
const clip = s => { while (new TextEncoder().encode(s).length > 60) s = s.slice(0, -1); return s; };

export default function App() {
  const [dev, setDev] = useState(null);
  const [st, setSt] = useState(null);
  const [list, setList] = useState([]);       // [{id, size, name}]
  const [vol, setVol] = useState(null);       // đang kéo thanh âm lượng
  const [job, setJob] = useState(null);       // { id, text, p, err } đang nạp
  const [trim, setTrim] = useState(null);     // { id, name, pcm, room, resolve }
  const [msg, setMsg] = useState('');
  const file = useRef(null), target = useRef(NEW), volTimer = useRef(0), lv = useRef(-1);

  const sync = async d => {
    d ||= dev;
    const s = await d.state();
    setSt(s);
    if (s.lv !== lv.current) {  // danh sách đổi -> tải lại
      lv.current = s.lv;
      setList(await d.list());
    }
  };

  const onConnect = async () => {
    setMsg('');
    try {
      let d;
      d = await connect(() => sync(d).catch(() => {}), () => { setDev(null); setSt(null); });
      lv.current = -1;
      setDev(d);
      await sync(d);
    } catch (e) {
      if (e.name !== 'NotFoundError') setMsg('Không kết nối được: ' + e.message);  // NotFoundError = bấm Huỷ
    }
  };

  const cmd = async text => {
    try { await dev.send(text); await sync(); } catch (e) { setMsg(e.message); }
  };

  const onVol = v => {
    setVol(v);
    clearTimeout(volTimer.current);
    volTimer.current = setTimeout(() => dev.send('v ' + v).catch(() => {}), 80);
  };
  const onVolDone = async () => {
    if (vol == null) return;
    clearTimeout(volTimer.current);
    await cmd('V ' + vol);
    setVol(null);
  };

  const pick = id => { target.current = id; file.current.click(); };

  async function load(id, f) {
    const name = clip(f.name.replace(/\.[^.]+$/, ''));
    const old = list.find(s => s.id === id);
    setJob({ id, text: f.type.startsWith('video') ? 'Đang tách âm thanh…' : 'Đang đọc file…' });
    try {
      const pcm = await decodeFile(f);
      setJob(null);
      const room = st.f + (old?.size || 0) - 65536;
      const seg = await new Promise(resolve => setTrim({ id, name, pcm, room, resolve }));
      setTrim(null);
      if (!seg) return;
      const t0 = Date.now();
      setJob({ id, text: 'Đang gửi… 0%', p: 0 });
      await dev.upload(id, encodePcm16(seg), name, p => setJob({ id, p,
        text: `Đang gửi… ${Math.round(p * 100)}%` + (p > 0.05 ? ` · còn ~${Math.round((Date.now() - t0) / p * (1 - p) / 1000)} s` : '') }));
      setJob(null);
    } catch (e) {
      setJob({ id, text: e.message, err: true });
    }
    await sync().catch(() => {});
  }

  useEffect(() => () => dev?.disconnect(), [dev]);

  if (!dev || !st) return (
    <main>
      <h1>SoundPad</h1>
      <div className="card">
        {supported() ? <>
          <p>Giữ phím <b>[1]+[4]</b> khoảng 1 giây để bật Bluetooth trên SoundPad (nghe chuông), rồi bấm Kết nối.</p>
          <button className="pri big" onClick={onConnect}>Kết nối</button>
        </> : (
          <p className="err">Trình duyệt này không có Web Bluetooth. Android / máy tính: dùng Chrome hoặc Edge.
            iPhone: dùng app <b>Bluefy</b>.</p>
        )}
        {msg && <p className="err">{msg}</p>}
      </div>
    </main>
  );

  const label = s => `${s.id + 1}. ${s.name || 'Không tên'}`;
  const v = vol ?? st.v;
  const working = job && !job.err;
  const status = id => job?.id === id && (
    <>
      <div className={job.err ? 'err' : 'mut'}>{job.text}</div>
      {job.p != null && <div className="bar"><i style={{ width: job.p * 100 + '%' }} /></div>}
    </>
  );

  return (
    <main>
      <header>
        <h1>SoundPad</h1>
        <span className={st.b >= 0 && st.b < 20 ? 'err' : 'mut'}>
          Pin {st.b >= 0 ? st.b + '%' : '?'}{st.mv > 0 && ` · ${(st.mv / 1000).toFixed(2).replace('.', ',')} V`}
        </span>
      </header>
      {msg && <div className="card err" onClick={() => setMsg('')}>{msg}</div>}

      <div className="card">
        <div className="mut">Âm lượng</div>
        <div className="row">
          <input type="range" min="0" max="100" value={v}
                 onChange={e => onVol(+e.target.value)} onPointerUp={onVolDone} onKeyUp={onVolDone} />
          <b>{v}%</b>
        </div>
      </div>

      <div className="card">
        <div className="mut">Chế độ phím</div>
        <div className="seg">
          <button aria-pressed={!st.r} onClick={() => cmd('r 0')}>Đơn</button>
          <button aria-pressed={!!st.r} onClick={() => cmd('r 1')}>Ngẫu nhiên</button>
        </div>
        {st.r ? <p className="mut">{list.length ? `Phím nào cũng phát ngẫu nhiên 1 trong ${list.length} âm đã nạp.` : 'Chưa nạp âm nào.'}</p> :
          [...Array(KEYS)].map((_, k) => (
            <label key={k} className="row">
              <b>Phím {k + 1}</b>
              <select value={st.m[k]} onChange={e => cmd(`m ${k} ${e.target.value}`)}>
                {!list.some(s => s.id === st.m[k]) && <option value={st.m[k]}>— chưa gán âm —</option>}
                {list.map(s => <option key={s.id} value={s.id}>{label(s)}</option>)}
              </select>
            </label>
          ))}
      </div>

      <h2>Âm thanh <span className="mut">{list.length}</span></h2>
      {list.map(s => {
        const keys = st.r ? [] : st.m.map((m, k) => (m === s.id ? k + 1 : 0)).filter(Boolean);
        return (
          <div key={s.id} className={'card slot' + (st.p === s.id ? ' on' : '')}>
            <div>
              <div className="name">{label(s)}</div>
              {status(s.id) || <div className="mut">{[secs(s.size), keys.length && 'Phím ' + keys.join(', ')].filter(Boolean).join(' · ')}</div>}
            </div>
            <div className="btns">
              <button className="pri" onClick={() => cmd('p ' + s.id)}>▶</button>
              <button disabled={working} onClick={() => pick(s.id)}>Đổi</button>
              <button disabled={working} onClick={() => confirm(`Xoá "${label(s)}"?`) && cmd('d ' + s.id)}>✕</button>
            </div>
          </div>
        );
      })}
      {job?.id === NEW && <div className="card">{status(NEW)}</div>}
      <button className="pri big" disabled={working} onClick={() => pick(NEW)}>+ Thêm âm thanh</button>
      <input type="file" hidden accept="audio/*,video/*" ref={file}
             onChange={e => { const f = e.target.files[0]; e.target.value = ''; f && load(target.current, f); }} />

      <p className="mut">Bộ nhớ còn trống khoảng {mins(st.f)} âm thanh. Thêm bao nhiêu âm cũng được tới khi đầy.</p>
      <button onClick={() => dev.disconnect()}>Ngắt kết nối</button>

      {trim && <Trim {...trim} onDone={trim.resolve} />}
    </main>
  );
}
