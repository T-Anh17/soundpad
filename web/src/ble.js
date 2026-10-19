// Nói chuyện với board qua Web Bluetooth. Giao thức: xem đầu esp/src/ble.cpp

const uuid = n => `7a1e000${n}-5b2c-4f3a-9d8e-1c2b3a4d5e6f`;
const SVC = uuid(1);
const enc = new TextEncoder(), dec = new TextDecoder();

export const supported = () => !!navigator.bluetooth;

// onChange: board báo trạng thái đổi. onLost: mất kết nối
export async function connect(onChange, onLost) {
  const device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SVC] }] });
  device.addEventListener('gattserverdisconnected', onLost);
  const svc = await (await device.gatt.connect()).getPrimaryService(SVC);
  const c = {};
  for (const [k, n] of [['state', 2], ['list', 3], ['evt', 4], ['cmd', 5], ['data', 6]])
    c[k] = await svc.getCharacteristic(uuid(n));

  // Trình duyệt chỉ cho một thao tác GATT mỗi lúc -> xếp hàng
  let q = Promise.resolve();
  const run = fn => (q = q.then(fn, fn));
  let uploading = false;

  const api = {
    state: () => run(async () => JSON.parse(dec.decode(await c.state.readValue()))),
    send: text => run(() => c.cmd.writeValueWithResponse(enc.encode(text))),
    disconnect: () => device.gatt.disconnect(),

    // Danh sách âm [{id, size, name}]. Board trả từng trang: ghi vị trí rồi đọc
    list: () => run(async () => {
      const out = [];
      for (let from = 0; from >= 0;) {
        await c.list.writeValueWithResponse(enc.encode(String(from)));
        const [next, ...lines] = dec.decode(await c.list.readValue()).split('\n');
        for (const l of lines) {
          const [id, size, ...name] = l.split('\t');
          if (l) out.push({ id: +id, size: +size, name: name.join(' ') });
        }
        from = +next;
      }
      return out;
    }),

    // Nạp âm số id (-1 = âm mới). bytes: PCM 16-bit. onProgress(0..1)
    async upload(id, bytes, name, onProgress) {
      uploading = true;
      try {
        const st = await api.state();
        const size = Math.min(512, (st.mtu || 23) - 3);
        await api.send(`u ${id} ${bytes.length} ${name}`);
        let noRsp = true;
        for (let i = 0, n = 0; i < bytes.length; i += size, n++) {
          const part = bytes.subarray(i, i + size);
          await run(async () => {
            if (noRsp) {
              try { return await c.data.writeValueWithoutResponse(part); } catch { noRsp = false; }
            }
            await c.data.writeValueWithResponse(part);
          });
          if (n % 16 === 0) onProgress(i / bytes.length);
        }
        await api.send('e');
        // board ghi xong mới báo "ok" (hoặc lý do lỗi)
        for (let t = 0; t < 100; t++) {
          const { up } = await api.state();
          if (up === 'ok') return onProgress(1);
          if (up && up !== 'busy') throw new Error(up);
          await new Promise(r => setTimeout(r, 200));
        }
        throw new Error('Board không trả lời');
      } finally {
        uploading = false;
      }
    },
  };

  await c.evt.startNotifications();
  c.evt.addEventListener('characteristicvaluechanged', () => !uploading && onChange());
  return api;
}
