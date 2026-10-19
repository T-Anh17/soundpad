// Đọc file âm thanh / video -> xử lý cho loa nhỏ -> PCM 16-bit gửi lên board

export const RATE = 22321;   // phải trùng SAMPLE_RATE trong esp/include/config.h
const MAX_FILE = 300e6;      // video lớn hơn thì điện thoại dễ hết RAM khi tách tiếng

export async function decodeFile(file) {
  const isVideo = file.type.startsWith('video');
  if (file.size > MAX_FILE) throw new Error(`File nặng ${Math.round(file.size / 1e6)} MB, chọn file dưới 300 MB`);
  const data = await file.arrayBuffer();
  let audio;
  try {
    // giải mã thẳng ở RATE cho đỡ tốn RAM với video dài
    audio = await new OfflineAudioContext(1, 1, RATE).decodeAudioData(data);
  } catch {
    throw new Error(isVideo ? 'Không tách được âm thanh khỏi video này' : 'Không đọc được file này');
  }
  const n = audio.length, ch = audio.numberOfChannels, out = new Float32Array(n);
  for (let c = 0; c < ch; c++) {
    const d = audio.getChannelData(c);
    for (let i = 0; i < n; i++) out[i] += d[i] / ch;
  }
  if (!n) throw new Error('File này không có âm thanh');
  return out;
}

// Loa nhỏ không phát nổi dải trầm (firmware còn lọc thêm dưới 1,2 kHz). Nhấn dải rõ chữ, nâng dải
// cao, nén động nhẹ để chỗ nói nhỏ to gần bằng chỗ nói to, rồi chuẩn hoá đỉnh 0,9
const HPF_HZ = 700, PRESENCE_HZ = 2500, PRESENCE_DB = 2, SHELF_HZ = 4000, SHELF_DB = 3;
const COMP_THRESHOLD = 0.35, COMP_RATIO = 2.5;

function biquad(d, b0, b1, b2, a0, a1, a2) {
  const out = new Float32Array(d.length);
  let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let i = 0; i < d.length; i++) {
    const x = d[i], y = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    out[i] = y;
  }
  return out;
}

const coef = f => { const w = 2 * Math.PI * f / RATE; return [Math.cos(w), Math.sin(w)]; };

function highPass(d) {
  const [cw, sw] = coef(HPF_HZ), al = sw / Math.SQRT2;
  return biquad(d, (1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
}

function presence(d) {
  const [cw, sw] = coef(PRESENCE_HZ), al = sw / 2, A = 10 ** (PRESENCE_DB / 40);
  return biquad(d, 1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
}

function highShelf(d) {
  const [cw, sw] = coef(SHELF_HZ), al = sw / Math.SQRT2, A = 10 ** (SHELF_DB / 40), r = 2 * Math.sqrt(A) * al;
  return biquad(d, A * ((A + 1) + (A - 1) * cw + r), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - r),
                (A + 1) - (A - 1) * cw + r, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - r);
}

// Đường bao đỉnh: lên trước đỉnh 5 ms (quét ngược), xuống dần trong 100 ms -> mức giảm đổi êm
function compress(d) {
  const n = d.length, rel = Math.exp(-1 / (RATE * 0.1)), att = Math.exp(-1 / (RATE * 0.005));
  let peak = 0;
  for (const v of d) peak = Math.max(peak, Math.abs(v));
  if (!peak) return d;
  const env = new Float32Array(n);
  for (let i = 0, e = 0; i < n; i++) env[i] = e = Math.max(Math.abs(d[i]) / peak, e * rel);
  for (let i = n - 1, e = 0; i >= 0; i--) env[i] = e = Math.max(env[i], e * att);
  const out = new Float32Array(n), k = 1 - 1 / COMP_RATIO;
  for (let i = 0; i < n; i++) out[i] = d[i] / peak * (env[i] > COMP_THRESHOLD ? (COMP_THRESHOLD / env[i]) ** k : 1);
  return out;
}

// Vị trí bắt đầu / kết thúc có tiếng
export function soundBounds(d) {
  let a = 0, b = d.length - 1;
  while (a < b && Math.abs(d[a]) < 0.01) a++;
  while (b > a && Math.abs(d[b]) < 0.01) b--;
  return [a, b];
}

// Cắt lặng hai đầu, lọc, nén, chuẩn hoá. null = đoạn gần như không có tiếng
export function prepare(d) {
  const [a, b] = soundBounds(d);
  if (b - a < RATE * 0.02) return null;
  d = d.subarray(Math.max(0, a - (RATE * 0.01 | 0)), Math.min(d.length, b + (RATE * 0.05 | 0)));
  d = compress(highShelf(presence(highPass(d))));
  let peak = 0;
  for (const v of d) peak = Math.max(peak, Math.abs(v));
  return peak ? d.map(v => v * 0.9 / peak) : null;
}

export function encodePcm16(f) {
  const out = new DataView(new ArrayBuffer(f.length * 2));
  for (let i = 0; i < f.length; i++) out.setInt16(i * 2, Math.max(-32768, Math.min(32767, Math.round(f[i] * 32767))), true);
  return new Uint8Array(out.buffer);
}

let ctx = null;
// Nghe thử trên điện thoại đúng thứ sẽ lưu. Trả hàm dừng
export function playPreview(seg, onEnd) {
  ctx ||= new AudioContext();
  ctx.resume();
  const buf = ctx.createBuffer(1, seg.length, RATE);
  buf.getChannelData(0).set(seg);
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.connect(ctx.destination);
  src.onended = onEnd;
  src.start();
  return () => { src.onended = null; src.stop(); };
}
