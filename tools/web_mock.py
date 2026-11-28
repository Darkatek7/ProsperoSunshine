#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The web page without a console: serves src/web/page and stands in for the host's API with made-up data, to look at a
change to the page in a browser on the PC. Signed in from the start; what the page posts is answered with ok and
changes nothing. The shapes follow src/web/web_server.cpp: the lists are the control socket's lines turned into
records (src/web/web_lines.hpp), so their values are strings.

Usage: python3 tools/web_mock.py [port] [state]     then open http://127.0.0.1:<port>/ (default 8790)
state: busy (default; a stream in trouble, a device asking to pair, a full history), quiet (nothing runs, a short
history) or empty (a fresh host: no device, no history).
"""
import http.server
import json
import os
import sys
import time

PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "web", "page")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8790
STATE = sys.argv[2] if len(sys.argv) > 2 else "busy"
NOW = int(time.time())
TV, DECK, PHONE = (f"{n:08X}-0000-4000-8000-00000000000{n}" for n in (1, 2, 3))


def fields(line, last):
    """web_lines.hpp's fields(): "key=value key=value last=rest of the line"."""
    record = {}
    while line:
        line = line.lstrip(" ")
        eq = line.find("=")
        if eq < 0:
            break
        key, line = line[:eq], line[eq + 1:]
        if key == last:
            record[key] = line
            break
        space = line.find(" ")
        record[key] = line if space < 0 else line[:space]
        line = "" if space < 0 else line[space + 1:]
    return record


def history_line(ago, secs, codec, hdr, mode, kbps, sent, rtt, asks, lost, internet, uuid, name):
    ended = NOW - ago if ago is not None else 0
    return (f"ended={ended} secs={secs} codec={codec} hdr={hdr} mode={mode} kbps={kbps} sent_kbps={sent} rtt_ms={rtt} "
            f"recoveries={asks} missing_frames={lost} internet={internet} uuid={uuid} name={name}")


HISTORY = [  # newest first, as HISTORY gives them
    history_line(120, 29, "HEVC", 0, "3840x2160@60", 62988, 2047, 1, 2, 0, 0, DECK, "Steam Deck"),
    history_line(600, 29, "H.264", 0, "1920x1080@60", 7308, 1640, 1, 9, 0, 1, DECK, "Steam Deck"),
    history_line(4000, 5400, "HEVC", 1, "3840x2160@60", 80000, 41230, 4, 3, 0, 0, TV, "Living room TV"),
    history_line(90000, 1260, "H.264", 0, "1280x720@60", 5000, 6890, 87, 412, 0, 1, "-",
                 "Phone on the train with a rather long name that has to wrap somewhere"),
    history_line(200000, 62, "H.264", 0, "-", 0, "-", "-", 0, 0, 0, TV, "Living room TV"),
    history_line(None, 300, "HEVC", 0, "2560x1440@60", 20000, 15200, 12, 14, 37, 0, DECK, "Steam Deck"),
]
STREAM = (f"uuid={TV} address=192.0.2.10 codec=HEVC hdr=1 mode=3840x2160@60 kbps=40000 secs=754 pads=1 role=capture "
          "channel=0 sent_kbps=61200 rtt_ms=3 recoveries=131 name=Living room TV")
DEVICES = [
    {"uuid": TV, "streaming": "1" if STATE == "busy" else "0", "mode": "3840x2160@60", "name": "Living room TV",
     "profiles": None, "follow_size": None},
    {"uuid": DECK, "streaming": "0", "mode": "1280x800@60", "name": "Steam Deck", "profiles": "free", "follow_size": True},
    {"uuid": PHONE, "streaming": "0", "mode": "", "name": "Phone", "profiles": "none", "follow_size": None},
]


def overview():
    busy, empty = STATE == "busy", STATE == "empty"
    stream = dict(fields(STREAM, "name"), health={
        "lossy": True, "over": True,
        "line": "Many lost frames: the device keeps asking for a new picture, so the host keeps sending keyframes. "
                "A lower resolution or bitrate helps. The host also sends far more video than was asked for.",
        "details": "The device asked for a new picture 9 times in the last 30 s (in 6 of 14 checks). The host sent "
                   "61200 kbit/s on average over the last 12 s; the stream asked for 40000 kbit/s."})
    return {
        "host": {"version": "0.0.0", "commit": "mock", "firmware": "13.00", "name": "PS5", "port": 47989,
                 "settings_pending": False, "follow_size_default": False},
        "status": {"running": "1" if busy else "0", "app": "PS5 Screen" if busy else "", "pending": "1" if busy else "0",
                   "lan": "0", "address": "192.0.2.10" if busy else "", "client": "Living room TV" if busy else ""},
        "pairings": [{"id": "1", "name": "roth", "address": "192.0.2.44"}] if busy else [],
        "devices": [] if empty else DEVICES,
        "trusted": [] if empty else [TV],
        "risk_ack": [],
        "menus": None,
        "streams": [stream] if busy else [],
        "history": [] if empty else [fields(line, "name") for line in (HISTORY if busy else HISTORY[:2])],
        "upnp": {"state": "off", "inner": None, "outer": None},
    }


SETTINGS = {
    "groups": [{"id": 0, "caption": "HOST", "note": ""}, {"id": 1, "caption": "NETWORK", "note": "Made-up options of the mock."},
               {"id": 4, "caption": "CONTROLLER", "note": ""}],
    "options": [
        {"key": "sunshine_name", "group": 0, "label": "Host name", "help": "The name Moonlight shows.", "kind": "text",
         "default": "network name", "choices": [], "min": 0, "max": 0, "step": 1, "app_only": False, "locked": False, "state": "",
         "value": None},
        {"key": "min_log_level", "group": 0, "label": "Log level", "help": "Detail in sunshine.log.", "kind": "choice",
         "default": "Info", "choices": [{"value": "debug", "label": "Debug"}, {"value": "info", "label": "Info"}], "min": 0,
         "max": 0, "step": 1, "app_only": False, "locked": False, "state": "", "value": "debug"},
        {"key": "port", "group": 1, "label": "Base port", "help": "Moonlight's ports count from it.", "kind": "number",
         "default": "47989", "choices": [], "min": 1029, "max": 65514, "step": 1, "app_only": False, "locked": False, "state": "",
         "value": None},
        {"key": "upnp", "group": 1, "label": "UPnP", "help": "Asks the router to forward the ports.", "kind": "toggle",
         "default": "off", "choices": [], "min": 0, "max": 0, "step": 1, "app_only": False, "locked": False, "state": "",
         "value": "on"},
        # The keyboard's three (app/src/psapp/conf.cpp): the first in the app's list, the other two in web_option_specs.
        {"key": "keyboard_mouse_profile_by_game", "group": 4, "label": "Keys by game",
         "help": "On: a keyboard and mouse get the keys that suit the game in front, from the host's title map.",
         "kind": "toggle", "default": "Off", "choices": [], "min": 0, "max": 0, "step": 1, "app_only": False, "locked": False,
         "state": "", "value": "1"},
        {"key": "keyboard_mouse_profile", "group": 4, "label": "Keyboard profile",
         "help": "The keys a keyboard and mouse play with where nothing picks a profile for the game.", "kind": "choice",
         "default": "Standard", "choices": [{"value": p, "label": p.capitalize()} for p in
                                            ["standard", "action", "shooter", "driving", "fighting", "platformer", "sports"]],
         "min": 0, "max": 0, "step": 1, "app_only": False, "locked": False, "state": "", "value": None},
        {"key": "keyboard_mouse_sensitivity", "group": 4, "label": "Mouse speed",
         "help": "Where the mouse's speed starts on a device that hasn't turned it with Ctrl+Alt and the wheel.", "kind": "number",
         "default": "100 %", "choices": [], "min": 10, "max": 1000, "step": 10, "app_only": False, "locked": False, "state": "",
         "value": None},
    ],
}
# /api/keyboard (src/web/web_server.cpp): the profile by game for the Apps tab; POST /api/keyboard/title changes it here.
KEYBOARD = {
    "default": "standard", "by_game": True, "pad": True, "problems": 1,
    "profiles": ["standard", "action", "shooter", "driving", "fighting", "platformer", "sports"],
    "own": ["kart"],
    "titles": [
        {"id": "PPSA00001", "named": "", "gets": "driving", "from": "title_id", "auto": "driving", "auto_from": "title_id",
         "name": "A game"},
        {"id": "PPSA00002", "named": "kart", "gets": "kart", "from": "list", "auto": "standard", "auto_from": "default",
         "name": "Another game"},
    ],
}


def set_keyboard(body):
    for title in KEYBOARD["titles"]:
        if title["id"] == body.get("id"):
            profile = body.get("profile")
            title["named"] = profile or ""
            title["gets"], title["from"] = (profile, "list") if profile else (title["auto"], title["auto_from"])


ANSWERS = {
    "/api/session": lambda: {"user": "owner", "settings_pending": False},
    "/api/overview": overview,
    "/api/users": lambda: {"users": [{"id": "10000001", "name": "User1"}, {"id": "10000002", "name": "Second player"}]},
    "/api/apps": lambda: {"apps": [{"id": "PPSA00001", "state": "shown", "name": "A game"},
                                   {"id": "PPSA00002", "state": "hidden", "name": "Another game"}]},
    "/api/settings": lambda: SETTINGS,
    "/api/keyboard": lambda: KEYBOARD,
}
LOG = "\n".join(f"[2026-01-01 12:00:{i:02d}.000]: Info: a line of the mock's log, number {i}" for i in range(40))
TYPES = {"html": "text/html; charset=utf-8", "js": "text/javascript", "css": "text/css"}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, body, content_type="application/json", code=200):
        body = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path in ANSWERS:
            return self.reply(json.dumps(ANSWERS[path]()))
        if path in ("/api/logs", "/api/diagnostics"):
            return self.reply(LOG, "text/plain; charset=utf-8")
        if path.startswith("/api/"):
            return self.reply(json.dumps({"error": "not in the mock"}), code=404)
        name = {"/": "index.html", "/app.js": "app.js", "/style.css": "style.css"}.get(path)
        if not name:
            return self.reply("", "text/plain", 404)
        with open(os.path.join(PAGE, name), "rb") as f:
            self.reply(f.read(), TYPES[name.rsplit(".", 1)[1]])

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        if self.path.split("?")[0] == "/api/keyboard/title":
            set_keyboard(json.loads(body or b"{}"))
        self.reply(json.dumps({"ok": True, "message": ""}))


if __name__ == "__main__":
    print(f"http://127.0.0.1:{PORT}/  ({STATE})")
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
