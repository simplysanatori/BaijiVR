#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A stand-in for Steam's DevTools port, for test-release.sh.  It answers the Runtime.evaluate expressions that
steam-entry.py sends (list, AddShortcut, SetShortcut*, SetCustomArtworkForApp), keeps the shortcuts in a JSON file
(program and start folder quoted, as Steam keeps them) and logs every call, long arguments cut.  The art is kept as
its decoded size and sha256.  PORT 0 picks a free port, written to STATE.json.port.
   mock-steam-cef.py PORT STATE.json LOG.txt"""
import base64, hashlib, json, re, socketserver, struct, sys, zlib
from http.server import BaseHTTPRequestHandler

PORT, STATE, LOG = int(sys.argv[1]), sys.argv[2], sys.argv[3]
S = r'("(?:[^"\\]|\\.)*")'  # a JSON string


def load():
    try:
        return json.load(open(STATE))
    except Exception:
        return {"shortcuts": {}}


def handle_js(expr):
    st = load()
    open(LOG, "a").write((expr if len(expr) < 400 else expr[:120] + "...(%d bytes)" % len(expr)) + "\n")
    sc = st["shortcuts"]
    if "allApps" in expr:
        return [[int(i), v["name"], v["exe"]] for i, v in sc.items()]
    m = re.search(r"AddShortcut\(" + S + ", " + S + r', "", ""\)', expr)
    if m:
        name, exe = json.loads(m.group(1)), json.loads(m.group(2))
        appid = (zlib.crc32(('"%s"' % exe + name).encode()) & 0xFFFFFFFF) | 0x80000000
        sc[str(appid)] = {"name": name, "exe": '"%s"' % exe, "start": "", "icon": "", "art": {}}
        json.dump(st, open(STATE, "w"), indent=1)
        return appid
    for m in re.finditer(r"SetShortcut(Name|Exe|StartDir|Icon)\((\d+), " + S + r"\)", expr):
        key = {"Name": "name", "Exe": "exe", "StartDir": "start", "Icon": "icon"}[m.group(1)]
        sc[m.group(2)][key] = json.loads(m.group(3))
    m = re.search(r"SetCustomArtworkForApp\((\d+), " + S + r', "png", (\d)\)', expr)
    if m:
        data = base64.b64decode(json.loads(m.group(2)))
        sc[m.group(1)]["art"][m.group(3)] = [len(data), hashlib.sha256(data).hexdigest()]
    json.dump(st, open(STATE, "w"), indent=1)
    return 1


def recv_exact(f, n):
    b = f.read(n)
    if len(b) < n:
        raise EOFError
    return b


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/json":
            body = json.dumps([{"title": "SharedJSContext",
                                "webSocketDebuggerUrl": "ws://127.0.0.1:%d/ws" % self.server.server_address[1]}]).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(404)
            return
        acc = base64.b64encode(__import__("hashlib").sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", acc)
        self.end_headers()
        f = self.rfile
        try:
            while True:
                b1, b2 = recv_exact(f, 2)
                n = b2 & 0x7F
                if n == 126:
                    n = struct.unpack(">H", recv_exact(f, 2))[0]
                elif n == 127:
                    n = struct.unpack(">Q", recv_exact(f, 8))[0]
                mask = recv_exact(f, 4) if b2 & 0x80 else b"\0\0\0\0"
                data = bytes(c ^ mask[i % 4] for i, c in enumerate(recv_exact(f, n)))
                if b1 & 0x0F == 8:
                    return
                req = json.loads(data)
                val = handle_js(req["params"]["expression"])
                out = json.dumps({"id": req["id"], "result": {"result": {"value": val}}}).encode()
                hdr = bytearray([0x81])
                if len(out) < 126:
                    hdr.append(len(out))
                elif len(out) < 65536:
                    hdr.append(126)
                    hdr += struct.pack(">H", len(out))
                else:
                    hdr.append(127)
                    hdr += struct.pack(">Q", len(out))
                self.wfile.write(bytes(hdr) + out)
                self.wfile.flush()
        except EOFError:
            return


class Srv(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


srv = Srv(("127.0.0.1", PORT), H)
open(STATE + ".port", "w").write(str(srv.server_address[1]))
srv.serve_forever()
