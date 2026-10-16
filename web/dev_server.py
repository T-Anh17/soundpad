"""Server giả lập SoundPad để xem/sửa giao diện web trên máy tính, không cần board.

Chạy:  python web/dev_server.py     rồi mở http://localhost:8790

API (firmware sẽ làm y hệt):
  GET  /                 trang web
  GET  /info             {vol, bat, free, total, playing, keys:[{name, size}]}
  GET  /state            {playing, vol, bat}      (trang hỏi mỗi giây)
  POST /play?btn=N       phát thử phím N (1..4)
  POST /volume?v=0..100[&save=1]
  POST /delete?btn=N
  POST /upload?btn=N&size=BYTES&name=...   multipart, field "file" = PCM 16-bit little-endian 22321 Hz mono
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

state = {"vol": 70, "bat": 82, "playing": -1, "keys": [{"name": "", "size": 0} for _ in range(4)]}
stop_timer = None


def fake_play(i):
    """Giả lập đang phát trong đúng độ dài âm thanh."""
    global stop_timer
    if stop_timer:
        stop_timer.cancel()
    state["playing"] = i
    stop_timer = threading.Timer(state["keys"][i]["size"] / 2 / RATE, lambda: state.update(playing=-1))
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
            used = sum(k["size"] for k in state["keys"])
            return self.reply(200, json.dumps({**state, "free": TOTAL - used, "total": TOTAL}), "application/json")
        if path == "/state":
            return self.reply(200, json.dumps({"playing": state["playing"], "vol": state["vol"], "bat": state["bat"]}), "application/json")
        self.reply(404, "Not found")

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        i = int(q.get("btn", 0)) - 1

        if u.path == "/upload":
            boundary = self.headers["Content-Type"].split("boundary=")[1].encode()
            part = next(p for p in body.split(b"--" + boundary) if b"filename=" in p)
            data = part.split(b"\r\n\r\n", 1)[1][:-2]
            state["keys"][i] = {"name": q.get("name", ""), "size": len(data)}
            print(f"upload phím {i + 1}: {q.get('name')} ({len(data)} byte = {len(data) / 2 / RATE:.2f} s)")
        elif u.path == "/play":
            fake_play(i)
            print(f"phát phím {i + 1}")
        elif u.path == "/volume":
            state["vol"] = int(q["v"])
            if "save" in q:
                print(f"lưu âm lượng {state['vol']}%")
        elif u.path == "/delete":
            state["keys"][i] = {"name": "", "size": 0}
            print(f"xoá phím {i + 1}")
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
