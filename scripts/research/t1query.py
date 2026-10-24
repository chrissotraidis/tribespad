#!/usr/bin/env python3
"""Read-only Starsiege: Tribes (Tribes 1) master + server status snapshot.

Packet formats follow github.com/TheKigen/t1net-go (Apache-2.0), commit be356a7:
  master.go   MasterQuery   10 03 FF 00 kk kk 00 00 -> 10 06 ...
  query.go    PingQuery     10 03 FF 00 kk kk 00 00 -> 10 04 .. F0 ..
              GameInfoQuery 10 07 FF 00 kk kk 00 00 -> 10 08 .. (Huffman bitstream)
  gamespy.go  GameSpyQuery  62 kk kk                -> 63 kk kk 62 ...
It only sends status queries. It never connects to a game, joins, or logs in.
"""
import json, os, random, socket, struct, sys, time, datetime
from concurrent.futures import ThreadPoolExecutor

MASTERS = ["t1m1.tribes1.co:28000", "t1m1.kigen.co:28000"]
# The masters' own MOTD (May 2024) lists six masters; --all-masters adds the other five (t1m1.kigen.co resolves to the same host as t1m1.tribes1.co).
OTHER_MASTERS = ["t1m2.tribes1.co:28000", "t1m3.tribes1.co:28000", "t1m1.pu.net:28000", "t1m2.pu.net:28000", "t1m3.pu.net:28000"]
TIMEOUT = 3.0
# Snapshots hold player names: keep them in the ignored generated/ folder.
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'generated', 't1-population')

def udp(addr, payload, timeout=TIMEOUT, multi=False):
    host, port = addr.rsplit(":", 1)
    ip = socket.gethostbyname(host)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    t0 = time.monotonic()
    s.sendto(payload, (ip, int(port)))
    out = []
    try:
        while True:
            data, src = s.recvfrom(65535)
            if src[0] != ip or src[1] != int(port):
                continue
            out.append((data, time.monotonic() - t0))
            if not multi:
                break
            s.settimeout(0.8)  # wait briefly for further packets
    except socket.timeout:
        pass
    finally:
        s.close()
    return ip, out

def pstr(b, i):
    n = b[i]; return b[i+1:i+1+n].decode("latin-1"), i+1+n

def master_query(addr):
    key = random.randrange(65536)
    pkt = bytes([0x10, 0x03, 0xFF, 0x00]) + struct.pack("<H", key) + b"\0\0"
    ip, replies = udp(addr, pkt, multi=True)
    res = {"master": addr, "ip": ip, "packets": [], "servers": [], "error": None}
    for data, rtt in replies:
        try:
            assert data[0] == 0x10 and data[1] == 0x06, "bad header"
            num, total = data[2], data[3]
            assert struct.unpack_from("<H", data, 4)[0] == key, "key mismatch"
            assert data[6] == 0x00 and data[7] == 0x66, "bad magic"
            i = 8
            name, i = pstr(data, i); motd, i = pstr(data, i)
            (count,) = struct.unpack_from(">H", data, i); i += 2
            servers = []
            for _ in range(count):
                assert data[i] == 6, "bad address record"
                a = ".".join(str(x) for x in data[i+1:i+5])
                (p,) = struct.unpack_from("<H", data, i+5); i += 7
                servers.append(f"{a}:{p}")
            res["packets"].append({"bytes": len(data), "num": num, "total": total, "name": name,
                                   "motd": motd, "count": count, "leftover": len(data) - i,
                                   "rtt_ms": round(rtt*1000)})
            res["servers"] += servers
        except Exception as e:
            res["error"] = f"{type(e).__name__}: {e}"
    if not replies:
        res["error"] = "timeout"
    return res

# ---- Huffman bitstream (bitstream.go) ----
FREQS = [0]*9 + [329, 21] + [0]*21 + [
 2809, 68, 0, 27, 0, 58, 3, 62, 4, 7, 0, 0, 15, 65, 554, 3,
 394, 404, 189, 117, 30, 51, 27, 15, 34, 32, 80, 1, 142, 3, 142, 39,
 0, 144, 125, 44, 122, 275, 70, 135, 61, 127, 8, 12, 113, 246, 122, 36,
 185, 1, 149, 309, 335, 12, 11, 14, 54, 151, 0, 0, 2, 0, 0, 211,
 0, 2090, 344, 736, 993, 2872, 701, 605, 646, 1552, 328, 305, 1240, 735, 1533, 1713,
 562, 3, 1775, 1149, 1469, 979, 407, 553, 59, 279, 31, 0, 0, 0, 68, 0] + [0]*128
assert len(FREQS) == 256

def build_huff():
    alnum = lambda c: chr(c).isascii() and chr(c).isalnum()
    leaf_pop = [FREQS[i] + 1 + (1 if alnum(i) else 0) for i in range(256)]
    nodes = [None]  # (pop, idx0, idx1)
    wraps = [("L", i) for i in range(256)]
    pop = lambda w: nodes[w[1]][0] if w[0] == "N" else leaf_pop[w[1]]
    det = lambda w: -(w[1] + 1) if w[0] == "L" else w[1]
    cur = 256
    while cur > 1:
        min1, min2, i1, i2 = 0xfffffffe, 0xffffffff, -1, -1
        for i in range(cur):
            p = pop(wraps[i])
            if p < min1:
                min2, i2, min1, i1 = min1, i1, p, i
            elif p < min2:
                min2, i2 = p, i
        nodes.append((pop(wraps[i1]) + pop(wraps[i2]), det(wraps[i1]), det(wraps[i2])))
        new = len(nodes) - 1
        merge, nuke = (i1, i2) if i1 < i2 else (i2, i1)
        wraps[merge] = ("N", new)
        if nuke != cur - 1:
            wraps[nuke] = wraps[cur - 1]
        cur -= 1
    nodes[0] = nodes[wraps[0][1]]
    return nodes
HUFF = build_huff()

class Bits:
    def __init__(s, b): s.b, s.pos, s.n = b, 0, len(b)*8
    def read(s, k):
        if s.pos + k > s.n: raise ValueError("out of bits")
        v = 0
        for j in range(k):
            if s.b[s.pos >> 3] & (1 << (s.pos & 7)): v |= 1 << j
            s.pos += 1
        return v
    def left(s): return s.n - s.pos
    def hstr(s):
        comp = s.read(1) == 1; length = s.read(8)
        if length == 0: return ""
        if not comp: return bytes(s.read(8) for _ in range(length)).decode("latin-1")
        buf = bytearray(length); i = 0; skip32 = False
        while i < length:
            idx = 0
            while idx >= 0:
                idx = HUFF[idx][2] if s.read(1) else HUFF[idx][1]
            sym = -(idx + 1); buf[i] = sym
            if i == 0 and sym >= 128:
                i -= 1; length -= 1; skip32 = True
            i += 1
        if skip32: s.read(32)
        return bytes(buf[:length]).decode("latin-1")

def native_info(addr):
    key = random.randrange(65536)
    _, r = udp(addr, bytes([0x10, 0x07, 0xFF, 0x00]) + struct.pack("<H", key) + b"\0\0")
    if not r: return None
    d, rtt = r[0]
    if d[0] != 0x10 or d[1] != 0x08 or struct.unpack_from("<H", d, 4)[0] != key:
        raise ValueError("bad native info reply")
    bs = Bits(d[8:]); o = {"rtt_ms": round(rtt*1000)}
    o["packet_version"] = bs.read(16)
    o["game"], o["version"], o["name"] = bs.hstr(), bs.hstr(), bs.hstr()
    o["players"], o["max"] = bs.read(8), bs.read(8)
    o["map"] = bs.hstr(); o["dedicated"] = bs.read(8) != 0; o["password"] = bs.read(8) != 0
    bs.read(32); o["mod"], o["type"], o["info"] = bs.hstr(), bs.hstr(), bs.hstr()
    bs.hstr(); bs.hstr()
    entries = []
    while bs.left() > 9:
        e = bs.hstr()
        if not e: break
        entries.append(e)
    o["player_names"] = [e[2:].split("\t")[0].strip() for e in entries if e[:2] == "10"]
    return o

def gamespy(addr):
    key = random.randrange(65536)
    _, r = udp(addr, bytes([0x62]) + struct.pack("<H", key))
    if not r: return None
    d, rtt = r[0]
    if d[0] != 0x63 or struct.unpack_from("<H", d, 1)[0] != key or d[3] != 0x62:
        raise ValueError("bad gamespy reply")
    i = 4; o = {"rtt_ms": round(rtt*1000)}
    o["game"], i = pstr(d, i); o["version"], i = pstr(d, i); o["name"], i = pstr(d, i)
    o["dedicated"], o["password"], o["players"], o["max"] = d[i] == 1, d[i+1] == 1, d[i+2], d[i+3]; i += 4
    i += 2
    o["mod"], i = pstr(d, i); o["type"], i = pstr(d, i); o["map"], i = pstr(d, i); o["info"], i = pstr(d, i)
    nteams = d[i]; i += 1
    _, i = pstr(d, i); _, i = pstr(d, i)
    for _ in range(nteams):
        _, i = pstr(d, i); _, i = pstr(d, i)
    pl = []
    for _ in range(o["players"]):
        ping, loss, team = d[i], d[i+1], d[i+2]; i += 3
        n, i = pstr(d, i); _, i = pstr(d, i)
        pl.append({"name": n, "ping": ping, "pl": loss, "team": team})
    o["player_list"] = pl
    return o

def clean(s): return "".join(c for c in s if c.isprintable()).strip()

def query_server(addr):
    rec = {"addr": addr, "answered": False}
    for fn, tag in ((gamespy, "gamespy"), (native_info, "native")):
        try:
            o = fn(addr)
        except Exception as e:
            rec[tag + "_error"] = f"{type(e).__name__}: {e}"; continue
        if o is None:
            rec[tag + "_error"] = "timeout"; continue
        rec["answered"] = True; rec["source"] = tag
        rec.update({k: o[k] for k in ("name", "map", "players", "max", "mod", "version", "password", "rtt_ms")})
        rec["name"], rec["map"] = clean(rec["name"]), clean(rec["map"])
        if tag == "gamespy":
            # No protocol field marks AI players. Heuristic: AI clients report ping 0
            # and packet loss 0 to the server. Treat this as an estimate.
            bots = [p for p in o["player_list"] if p["ping"] == 0 and p["pl"] == 0]
            rec["bots_heuristic"] = len(bots)
            rec["humans_estimate"] = rec["players"] - len(bots)
            rec["player_names"] = [clean(p["name"]) for p in o["player_list"]]
        else:
            rec["bots_heuristic"] = None; rec["humans_estimate"] = rec["players"]
            rec["player_names"] = [clean(n) for n in o["player_names"]]
        break
    return rec

def main():
    now = datetime.datetime.now(datetime.timezone.utc)
    snap = {"utc": now.strftime("%Y-%m-%d %H:%M:%S UTC"),
            "jst": now.astimezone(datetime.timezone(datetime.timedelta(hours=9))).strftime("%Y-%m-%d %H:%M JST"),
            "us_eastern_approx": (now - datetime.timedelta(hours=4)).strftime("%a %H:%M EDT"),
            "masters": [master_query(m) for m in MASTERS + (OTHER_MASTERS if "--all-masters" in sys.argv else [])]}
    union = sorted({s for m in snap["masters"] for s in m["servers"]})
    with ThreadPoolExecutor(16) as ex:
        servers = list(ex.map(query_server, union))
    snap["servers"] = servers
    ans = [s for s in servers if s["answered"]]
    pop = [s for s in ans if s["players"] > 0]
    snap["summary"] = {
        "listed_per_master": {m["master"]: len(m["servers"]) for m in snap["masters"]},
        "listed_union": len(union), "answered": len(ans),
        "servers_with_players": len(pop),
        "total_players_reported": sum(s["players"] for s in ans),
        "total_bots_heuristic": sum(s["bots_heuristic"] or 0 for s in ans),
        "total_humans_estimate": sum(s["humans_estimate"] for s in ans),
        "servers_with_humans_estimate": sum(1 for s in ans if s["humans_estimate"] > 0),
        "reported_versions": dict(sorted(__import__("collections").Counter(s["version"] for s in ans).items())),
        "mods_with_players": dict(__import__("collections").Counter(s["mod"].strip() for s in ans if s["players"] > 0)),
    }
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "snapshot-" + now.strftime("%Y%m%dT%H%M%SZ") + ".json")
    with open(path, "w") as f: json.dump(snap, f, indent=1)
    print(snap["utc"], "|", snap["jst"], "|", snap["us_eastern_approx"])
    for m in snap["masters"]:
        print("master", m["master"], m["ip"], "packets", [(p["bytes"], p["num"], p["total"], p["count"], p["leftover"]) for p in m["packets"]], "servers", len(m["servers"]), "err", m["error"])
        if m["packets"]: print("  name:", clean(m["packets"][0]["name"]), "| motd:", clean(m["packets"][0]["motd"])[:120])
    print("summary", json.dumps(snap["summary"]))
    for s in sorted(ans, key=lambda s: -s["players"]):
        if s["players"] > 0:
            print(f'  {s["players"]:>2}/{s["max"]:<2} bots~{s["bots_heuristic"]} {s["name"][:40]:40} map={s["map"][:22]:22} mod={s["mod"][:12]} via={s["source"]}')
    print("saved", path)

if __name__ == "__main__":
    main()

