"""Server giả lập SoundPad để xem/sửa giao diện web trên máy tính, không cần board.

Chạy:  python web/dev_server.py     rồi mở http://localhost:8790

API (firmware sẽ làm y hệt):
  GET  /                 trang web
  GET  /info             {vol, bat, mv, free, total, playing, random, map:[ô của phím 1..4], sounds:[{name, size} x10]}
  GET  /state            {playing, vol, bat, mv}  (trang hỏi mỗi giây; playing = ô đang phát, đếm từ 0)
  POST /play?slot=N      phát thử ô N (1..10)
  POST /volume?v=0..100[&save=1]
  POST /delete?slot=N
  POST /assign?key=K&slot=N   chế độ đơn: phím K (1..4) phát ô N
  POST /mode?random=0|1
  POST /upload?slot=N&size=BYTES&name=...   multipart, field "file" = PCM 16-bit little-endian 22321 Hz mono
"""
import http.server
import json
import os
import sys
import threading
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 22321  # trùng RATE trong index.html
TOTAL = 13_400_000  # ~12.8 MB như phân vùng FFat thật (esp/partitions.csv)

SOUNDS = 10  # trùng SOUND_COUNT trong esp/include/config.h

state = {"vol": 70, "bat": 82, "mv": 3980, "playing": -1, "random": False, "map": [0, 1, 2, 3],
         "sounds": [{"name": "", "size": 0} for _ in range(SOUNDS)]}
stop_timer = None


def fake_play(i):
    """Giả lập đang phát trong đúng độ dài âm thanh."""
    global stop_timer
    if stop_timer:
        stop_timer.cancel()
    state["playing"] = i
    stop_timer = threading.Timer(state["sounds"][i]["size"] / 2 / RATE, lambda: state.update(playing=-1))
    stop_timer.start()


class Handler(http.server.BaseHTTPRequestHandler):
    def reply(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/":
            with open(os.path.join(HERE, "index.html"), encoding="utf-8") as f:
                return self.reply(200, f.read(), "text/html; charset=utf-8")
        if path == "/info":
            used = sum(k["size"] for k in state["sounds"])
            return self.reply(200, json.dumps({**state, "free": TOTAL - used, "total": TOTAL}), "application/json")
        if path == "/state":
            return self.reply(200, json.dumps({"playing": state["playing"], "vol": state["vol"], "bat": state["bat"], "mv": state["mv"]}), "application/json")
        self.reply(404, "Not found")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        i = int(q.get("slot", 0)) - 1

        if u.path == "/upload":
            boundary = self.headers["Content-Type"].split("boundary=")[1].encode()
            part = next(p for p in body.split(b"--" + boundary) if b"filename=" in p)
            data = part.split(b"\r\n\r\n", 1)[1][:-2]
            state["sounds"][i] = {"name": q.get("name", ""), "size": len(data)}
            print(f"upload ô {i + 1}: {q.get('name')} ({len(data)} byte = {len(data) / 2 / RATE:.2f} s)")
        elif u.path == "/play":
            fake_play(i)
            print(f"phát ô {i + 1}")
        elif u.path == "/volume":
            state["vol"] = int(q["v"])
            if "save" in q:
                print(f"lưu âm lượng {state['vol']}%")
        elif u.path == "/delete":
            state["sounds"][i] = {"name": "", "size": 0}
            print(f"xoá ô {i + 1}")
        elif u.path == "/assign":
            state["map"][int(q["key"]) - 1] = i
            print(f"phím {q['key']} <- ô {i + 1}")
        elif u.path == "/mode":
            state["random"] = q.get("random") == "1"
            print("chế độ", "ngẫu nhiên" if state["random"] else "đơn")
        else:
            return self.reply(404, "Not found")
        self.reply(200, "OK")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")  # console Windows in được tiếng Việt
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8790
    print(f"Mở http://localhost:{port}")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
