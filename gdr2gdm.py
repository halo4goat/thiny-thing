#!/usr/bin/env python3
"""Convert a Geometry Dash .gdr replay (MessagePack, e.g. from xdBot) into GDMACRO1 for GdMacroMenu.

Each input event's frame is mapped to the player-1 X position recorded for that same frame
in the replay's frameFixes. Refuses anything it cannot map exactly (missing frame, second
player, other buttons, non-alternating events). Standard library only.

usage: python gdr2gdm.py in.gdr out.gdm
"""
import struct
import sys


class Reader:
    def __init__(self, data):
        self.b, self.i = data, 0

    def take(self, n):
        if self.i + n > len(self.b):
            raise ValueError("truncated MessagePack data at byte %d" % self.i)
        v = self.b[self.i:self.i + n]
        self.i += n
        return v

    def u(self, fmt, n):
        return struct.unpack(fmt, self.take(n))[0]

    def value(self):
        t = self.take(1)[0]
        if t <= 0x7F: return t
        if t >= 0xE0: return t - 256
        if 0x80 <= t <= 0x8F: return self.map(t & 0xF)
        if 0x90 <= t <= 0x9F: return self.arr(t & 0xF)
        if 0xA0 <= t <= 0xBF: return self.take(t & 0x1F).decode("utf8", "replace")
        simple = {0xC0: None, 0xC2: False, 0xC3: True}
        if t in simple: return simple[t]
        if t == 0xC4: return bytes(self.take(self.u(">B", 1)))
        if t == 0xC5: return bytes(self.take(self.u(">H", 2)))
        if t == 0xC6: return bytes(self.take(self.u(">I", 4)))
        nums = {0xCA: (">f", 4), 0xCB: (">d", 8), 0xCC: (">B", 1), 0xCD: (">H", 2), 0xCE: (">I", 4),
                0xCF: (">Q", 8), 0xD0: (">b", 1), 0xD1: (">h", 2), 0xD2: (">i", 4), 0xD3: (">q", 8)}
        if t in nums: return self.u(*nums[t])
        if t == 0xD9: return self.take(self.u(">B", 1)).decode("utf8", "replace")
        if t == 0xDA: return self.take(self.u(">H", 2)).decode("utf8", "replace")
        if t == 0xDB: return self.take(self.u(">I", 4)).decode("utf8", "replace")
        if t == 0xDC: return self.arr(self.u(">H", 2))
        if t == 0xDD: return self.arr(self.u(">I", 4))
        if t == 0xDE: return self.map(self.u(">H", 2))
        if t == 0xDF: return self.map(self.u(">I", 4))
        raise ValueError("unsupported MessagePack type 0x%02X at byte %d" % (t, self.i - 1))

    def arr(self, n):
        return [self.value() for _ in range(n)]

    def map(self, n):
        d = {}
        for _ in range(n):
            k = self.value()
            d[k] = self.value()
        return d


def convert(src, dst):
    data = open(src, "rb").read()
    r = Reader(data)
    replay = r.value()
    if r.i != len(data):
        raise SystemExit("trailing bytes after the MessagePack document (%d of %d used)" % (r.i, len(data)))
    for key in ("inputs", "frameFixes"):
        if not isinstance(replay.get(key), list):
            raise SystemExit("missing '%s' list: not a GDR this converter understands" % key)

    xs = {}
    for ff in replay["frameFixes"]:
        p1 = ff.get("p1")
        if isinstance(p1, dict) and "x" in p1:
            xs[ff["frame"]] = float(p1["x"])

    out, down_state = [], False
    for n, ev in enumerate(replay["inputs"]):
        if ev.get("2p") or ev.get("btn") != 1:
            raise SystemExit("input #%d uses player 2 or button %r; the menu has one click input" % (n, ev.get("btn")))
        if ev["frame"] not in xs:
            raise SystemExit("input #%d at frame %d has no X sample in frameFixes" % (n, ev["frame"]))
        if bool(ev["down"]) == down_state:
            raise SystemExit("input #%d does not alternate press/release" % n)
        down_state = bool(ev["down"])
        out.append((xs[ev["frame"]], down_state))
    if down_state:
        raise SystemExit("replay ends with the button held")
    if any(b[0] < a[0] for a, b in zip(out, out[1:])):
        raise SystemExit("X goes backwards between inputs")

    with open(dst, "w", newline="\n") as f:
        f.write("GDMACRO1\n")
        for x, d in out:
            f.write("%.4f %d\n" % (x, 1 if d else 0))
    lvl = replay.get("level", {})
    print("level=%r bot=%s %s game=%s fps=%s" % (lvl.get("name"), replay.get("bot", {}).get("name"),
          replay.get("bot", {}).get("version"), replay.get("gameVersion"), replay.get("framerate")))
    print("wrote %d actions (%d presses) to %s" % (len(out), sum(1 for _, d in out if d), dst))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(sys.argv[1], sys.argv[2])
