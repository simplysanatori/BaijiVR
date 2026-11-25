#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Puts Baiji VR in the Steam library through Steam's local DevTools port (127.0.0.1:8080), with its art.

  steam-entry.py --exe ~/baiji/bin/baiji-vr --art DIR [--icon FILE] [--name "Baiji VR"] [--dry-run]

Finds the entry whose program is --exe, or whose name is "Baiji VR" or "Baiji", and updates its name, program, start
folder, icon and art (so its appid and play time stay); adds one when there is none; with more than one it leaves
them all and says so.  Every string goes into the JavaScript JSON-encoded.  --dry-run prints the calls and does
not connect.  The port is BAIJI_VR_CEF_PORT (default 8080).  Standard library only.
Exit status: 0 done (or a dry run), 10 Steam could not be reached or refused (nothing is wrong with the files), 11 more
than one entry matched (none was changed), 2 a bad call (no program, no art).
"""
import argparse, base64, json, os, socket, struct, sys, urllib.request

APP_TYPE_SHORTCUT = 1073741824
# file -> Steam asset type (0 portrait capsule, 1 hero, 2 logo, 3 wide capsule)
ART = [("portrait.png", 0), ("hero.png", 1), ("logo.png", 2), ("wide.png", 3)]
q = json.dumps


class WS:
    """Just enough RFC 6455 for DevTools requests on loopback."""

    def __init__(self, url):
        host_port, path = url[len("ws://"):].split("/", 1)
        host, port = host_port.rsplit(":", 1)
        self.s = socket.create_connection((host, int(port)), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, host_port, key)).encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise EOFError("closed during the handshake")
            buf += chunk
        if b" 101 " not in buf.split(b"\r\n", 1)[0]:
            raise OSError("the websocket handshake was refused")
        self.rest = buf.split(b"\r\n\r\n", 1)[1]

    def _read(self, n):
        while len(self.rest) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise EOFError("closed")
            self.rest += chunk
        out, self.rest = self.rest[:n], self.rest[n:]
        return out

    def send(self, text):
        data = text.encode()
        mask = os.urandom(4)
        n = len(data)
        if n < 126:
            head = bytes([0x81, 0x80 | n])
        elif n < 65536:
            head = bytes([0x81, 0x80 | 126]) + struct.pack(">H", n)
        else:
            head = bytes([0x81, 0x80 | 127]) + struct.pack(">Q", n)
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        msg = b""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            payload = self._read(n)
            if b0 & 0x0F in (0x9, 0xA):  # ping, pong
                continue
            msg += payload
            if b0 & 0x80:
                return msg.decode()


class Steam:
    def __init__(self, port):
        with urllib.request.urlopen("http://127.0.0.1:%d/json" % port, timeout=3) as r:
            targets = json.load(r)
        url = next((t["webSocketDebuggerUrl"] for t in targets if t.get("title") == "SharedJSContext"), None)
        if not url:
            raise OSError("Steam's SharedJSContext was not found")
        self.ws = WS(url)
        self.n = 0

    def js(self, expression):
        self.n += 1
        self.ws.send(json.dumps({"id": self.n, "method": "Runtime.evaluate", "params": {
            "expression": expression, "awaitPromise": True, "returnByValue": True}}))
        while True:
            r = json.loads(self.ws.recv())
            if r.get("id") == self.n:
                break
        res = r.get("result", {})
        if "exceptionDetails" in res:
            raise OSError("Steam rejected the request: " + json.dumps(res["exceptionDetails"])[:300])
        return res.get("result", {}).get("value")


LIST_JS = """(() => appStore.allApps.filter(a => a.app_type === %d).map(a => {
  const d = appStore.GetAppOverviewByAppID(a.appid);
  return [a.appid, a.display_name, d ? d.shortcut_exe : ""];
}))()""" % APP_TYPE_SHORTCUT


def add_js(name, exe):
    return "SteamClient.Apps.AddShortcut(%s, %s, \"\", \"\")" % (q(name), q(exe))


def update_js(appid, name, exe, start, icon):
    s = ["SteamClient.Apps.SetShortcutName(%d, %s);" % (appid, q(name)),
         "SteamClient.Apps.SetShortcutExe(%d, %s);" % (appid, q('"' + exe + '"')),
         "SteamClient.Apps.SetShortcutStartDir(%d, %s);" % (appid, q('"' + start + '"'))]
    if icon:
        s.append("SteamClient.Apps.SetShortcutIcon(%d, %s);" % (appid, q(icon)))
    return "(() => { %s return 1; })()" % " ".join(s)


def art_js(appid, b64, kind):
    return "SteamClient.Apps.SetCustomArtworkForApp(%d, %s, \"png\", %d)" % (appid, q(b64), kind)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--art", required=True)
    ap.add_argument("--icon", default="")
    ap.add_argument("--name", default="Baiji VR")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    names = {a.name, "Baiji"}
    start = os.path.dirname(a.exe)
    art = []
    for f, kind in ART:
        p = os.path.join(a.art, f)
        if not os.path.isfile(p):
            print("steam-entry: %s is missing" % p, file=sys.stderr)
            return 2
        with open(p, "rb") as fh:
            art.append((f, kind, base64.b64encode(fh.read()).decode()))
    if a.dry_run:
        print("[dry run] Steam is not contacted; its DevTools port is assumed reachable")
        print("[dry run] would look for an entry whose program is %s or named %s:" % (a.exe, " / ".join(sorted(names))))
        print("[dry run]   " + " ".join(LIST_JS.split()))
        print("[dry run] none: " + add_js(a.name, a.exe))
        print("[dry run] then, on the found or new appid (shown as 0):")
        print("[dry run]   " + update_js(0, a.name, a.exe, start, a.icon))
        for f, kind, b64 in art:
            print("[dry run]   " + art_js(0, "<%s: %d bytes of base64>" % (f, len(b64)), kind))
        return 0
    try:
        st = Steam(int(os.environ.get("BAIJI_VR_CEF_PORT", "8080")))
        rows = st.js(LIST_JS) or []
        mine = [r for r in rows if r[1] in names or (r[2] or "").strip('"') == a.exe]
        if len(mine) > 1:
            print("steam-entry: Steam has %d Baiji entries (%s); none was changed. Remove the extras and run install.sh again."
                  % (len(mine), ", ".join("%s (%s)" % (r[1], r[0]) for r in mine)))
            return 11
        if mine:
            appid = int(mine[0][0])
            print("Steam already has an entry named '%s' (appid %d); updating it" % (mine[0][1], appid))
        else:
            appid = st.js(add_js(a.name, a.exe))
            if not isinstance(appid, int) or appid <= 0:
                raise OSError("Steam returned no appid (%r)" % (appid,))
            print("added '%s' to the Steam library (appid %d)" % (a.name, appid))
        st.js(update_js(appid, a.name, a.exe, start, a.icon))
        for f, kind, b64 in art:
            st.js(art_js(appid, b64, kind))
        print("set the name, program, icon and art of the entry")
        return 0
    except (OSError, EOFError, ValueError) as e:
        print("steam-entry: Steam could not be reached or refused (%s)" % e, file=sys.stderr)
        return 10


if __name__ == "__main__":
    sys.exit(main())
