#!/usr/bin/env python3
"""End-to-end test of winagent.exe under CrossOver Wine (DSMP bottle only), run on the Mac.
A stand-in hub and a fake game control port run here; the agent runs in Wine and connects out to them.
Usage: python3 test_agent.py          (build first: ./build_agent.sh)
Prints PASS/FAIL per op; exit code 0 only when every check passes."""
import base64, json, os, secrets, signal, socket, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "build", "winagent.exe")
WINE = os.path.expanduser("~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine")
GUARD = os.path.expanduser("~/ds_multiplayer/win/tools/dsmp_guard.py")
BOTTLE = "DSMP"   # never any other bottle
TD = r"%TEMP%\winbot_test"
RESULTS = []


def zpath(p):
    return "Z:" + p.replace("/", "\\")


def check(name, cond, detail=""):
    RESULTS.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else "  -> " + str(detail)[:400]), flush=True)
    return bool(cond)


# ---------------------------------------------------------------- stand-in hub
class Conn:
    def __init__(self, sock, hello):
        self.sock, self.hello, self.buf, self.next_id = sock, hello, b"", 1

    def readline(self, timeout):
        self.sock.settimeout(timeout)
        while b"\n" not in self.buf:
            d = self.sock.recv(1 << 20)
            if not d:
                raise EOFError("hub connection closed")
            self.buf += d
        line, self.buf = self.buf.split(b"\n", 1)
        return line

    def call(self, op, timeout=30, **kw):
        rid = self.next_id
        self.next_id += 1
        req = dict(kw, id=rid, op=op)
        self.sock.sendall(json.dumps(req).encode() + b"\n")   # ensure_ascii: non-ASCII goes as \uXXXX
        resp = json.loads(self.readline(timeout).decode("utf-8"))
        if resp.get("id") != rid:
            raise RuntimeError("id mismatch %r" % resp)
        return resp


class Hub:
    def __init__(self, token, port=0):
        self.token, self.conns, self.rejected, self.cv = token, [], 0, threading.Condition()
        self.port, self.lsock, self.all = port, None, []
        self.start()

    def start(self):
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", self.port))
        s.listen(16)
        self.port, self.lsock = s.getsockname()[1], s
        threading.Thread(target=self.accept_loop, args=(s,), daemon=True).start()

    def accept_loop(self, s):
        while True:
            try:
                c, _ = s.accept()
            except OSError:
                return
            threading.Thread(target=self.on_conn, args=(c,), daemon=True).start()

    def on_conn(self, c):
        self.all.append(c)
        try:
            cn = Conn(c, None)
            hello = json.loads(cn.readline(10).decode("utf-8"))
        except Exception:
            c.close()
            return
        with self.cv:
            if hello.get("token") != self.token:
                self.rejected += 1
                c.close()
            else:
                cn.hello = hello
                self.conns.append(cn)
            self.cv.notify_all()

    def wait_conns(self, n, timeout):
        end = time.time() + timeout
        with self.cv:
            while len(self.conns) < n and time.time() < end:
                self.cv.wait(0.2)
            return list(self.conns)

    def stop(self):
        self.lsock.close()
        for c in self.all:
            try:
                c.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            c.close()
        self.conns, self.all = [], []


# ---------------------------------------------------------------- fake game control port
def fake_game():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    s.listen(8)

    def serve(c):
        f = c.makefile("rb")
        for line in f:
            line = line.rstrip(b"\n").decode("utf-8")
            if line == "NOANSWER":
                continue
            c.sendall(json.dumps({"ok": True, "echo": line}).encode() + b"\n")
        c.close()

    def loop():
        while True:
            c, _ = s.accept()
            threading.Thread(target=serve, args=(c,), daemon=True).start()

    threading.Thread(target=loop, daemon=True).start()
    return s.getsockname()[1]


def mac_agent_pids():
    out = subprocess.run(["ps", "-axo", "pid=,command="], capture_output=True, text=True).stdout
    return {int(l.split()[0]) for l in out.splitlines() if "winagent.exe" in l.lower() and "test_agent" not in l}


def b64(s):
    return base64.b64encode(s.encode("utf-8")).decode()


def unb64(s):
    return base64.b64decode(s).decode("utf-8", "replace")


def in_thread(fn):
    box = {}

    def run():
        t0 = time.time()
        try:
            box["r"] = fn()
        except Exception as e:
            box["e"] = e
        box["t"] = time.time() - t0
    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t, box


# ---------------------------------------------------------------- main
def main():
    if not os.path.exists(EXE):
        sys.exit("build first: ./build_agent.sh")
    # the window guard must watch DSMP before anything runs in Wine (it exits at once when one already runs)
    subprocess.Popen([sys.executable, GUARD, "1800"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=True)
    token = secrets.token_hex(16)
    hub = Hub("WRONG-" + token)            # phase 1: the hub expects another token -> every hello is rejected
    gport = fake_game()
    before = mac_agent_pids()
    subprocess.run([WINE, "--bottle", BOTTLE, "--no-wait", zpath(EXE), "127.0.0.1:%d" % hub.port, token, "4", "5"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=80)
    mine = set()
    try:
        end = time.time() + 60
        while time.time() < end and hub.rejected < 6:
            time.sleep(0.3)
        mine = mac_agent_pids() - before
        check("wrong token rejected (agent keeps retrying)", hub.rejected >= 6 and not hub.conns, hub.rejected)
        hub.token = token
        conns = hub.wait_conns(4, 20)
        check("hello x4", len(conns) == 4 and sorted(c.hello["conn"] for c in conns) == [0, 1, 2, 3]
              and all(c.hello["hello"] == "winagent" and c.hello["ver"] == 1 for c in conns), [c.hello for c in conns])
        if len(conns) < 2:
            raise RuntimeError("agent did not connect")
        A, B = conns[0], conns[1]
        mine |= mac_agent_pids() - before

        r = A.call("ping")
        agent_pid = r.get("pid")
        check("ping", r["ok"] and agent_pid == A.hello["pid"] and abs(r["time"] - time.time()) < 120 and r["ver"] == 1, r)

        # files (TD under the bottle's %TEMP%; nothing is ever deleted)
        f = TD + r"\data.txt"
        r = A.call("write", path=f, b64=b64("0123456789"), mkdirs=True)
        r2 = A.call("read", path=f, offset=-4)
        check("write (mkdirs, replace) + read negative offset",
              r["ok"] and r["size"] == 10 and r2["ok"] and unb64(r2["b64"]) == "6789" and r2["offset"] == 6
              and r2["size"] == 10, (r, r2))
        r = A.call("stat", path=f + ".winbot.tmp")
        check("write leaves no .winbot.tmp", r["ok"] and r["exists"] is False, r)
        A.call("write", path=f, b64=b64("abc"))
        r = A.call("write", path=f, b64=b64("déf"), append=True)
        r2 = A.call("read", path=f)
        check("write append", r["ok"] and r["size"] == 7 and unb64(r2["b64"]) == "abcdéf", (r, r2))
        r = A.call("stat", path=f)
        r2 = A.call("stat", path=TD)
        r3 = A.call("stat", path=TD + r"\missing.txt")
        check("stat", r["ok"] and r["exists"] and not r["dir"] and r["size"] == 7 and r["mtime"] > 1.7e9
              and r2["dir"] is True and r3["ok"] and r3["exists"] is False, (r, r2, r3))
        uni = "été_\U0001F600.txt"
        A.call("write", path=TD + "\\" + uni, b64=b64("u"))
        r = A.call("ls", path=TD)
        names = {e["name"]: e for e in r.get("entries", [])}
        check("ls (+ UTF-16 names, surrogate pair)", r["ok"] and uni in names and names["data.txt"]["size"] == 7
              and names[uni]["dir"] is False, r)
        A.call("write", path=TD + r"\mv_src.txt", b64=b64("mv"))
        r = A.call("move", src=TD + r"\mv_src.txt", dst=TD + r"\sub\deeper\mv_dst.txt", mkdirs=True)
        r2 = A.call("read", path=TD + r"\sub\deeper\mv_dst.txt")
        r3 = A.call("stat", path=TD + r"\mv_src.txt")
        check("move (mkdirs, replace)", r["ok"] and unb64(r2.get("b64", "")) == "mv" and r3["exists"] is False, (r, r2, r3))

        # wait_file
        wf = TD + r"\log.txt"
        r = A.call("write", path=wf, b64=b64("old READY line\n"))
        start = r["size"]
        t, box = in_thread(lambda: A.call("wait_file", path=wf, any=["READY", "zzz"], offset=start, timeout_ms=10000))
        time.sleep(0.8)
        B.call("write", path=wf, b64=b64("partial "), append=True)
        time.sleep(0.5)
        r2 = B.call("write", path=wf, b64=b64("READY now\r\nmore\n"), append=True)
        t.join(15)
        r = box.get("r") or {}
        check("wait_file found (file growing, partial line, offset skips old line)",
              r.get("found") is True and r.get("line") == "partial READY now"
              and r.get("offset") == start + len("partial READY now\r\n"), (r, box.get("e")))
        r = A.call("wait_file", path=wf, any=["NEVER"], offset=-5, timeout_ms=400)
        size_now = r2["size"]
        r3 = A.call("wait_file", path=TD + r"\nofile.txt", any=["x"], timeout_ms=300)
        check("wait_file timeout (+ missing file)", r["ok"] and r["found"] is False and r["offset"] == size_now
              and r3["ok"] and r3["found"] is False, (r, r3))

        # parallel: a 2 s wait on A must not block B
        t, box = in_thread(lambda: A.call("wait_file", path=TD + r"\nofile.txt", any=["x"], timeout_ms=2000))
        time.sleep(0.2)
        t0 = time.time()
        r = B.call("ping")
        dt = time.time() - t0
        busy = t.is_alive()
        t.join(10)
        check("two requests in parallel on two connections", r["ok"] and dt < 1.0 and busy and box.get("r", {}).get("ok"),
              (dt, busy, box))

        # game
        line = json.dumps({"cmd": "status", "s": "é\U0001F600"}, ensure_ascii=False)
        r = A.call("game", line=line, port=gport, timeout_ms=5000)
        ok1 = r["ok"] and r["port"] == gport and json.loads(r["resp"]) == {"ok": True, "echo": line}
        A.call("write", path=TD + r"\ctl.port", b64=b64("%d\n" % gport))
        r2 = A.call("game", line="ping2", port_file=TD + r"\ctl.port", timeout_ms=5000)
        ok2 = r2["ok"] and r2["port"] == gport and json.loads(r2["resp"])["echo"] == "ping2"
        t0 = time.time()
        r3 = A.call("game", line="NOANSWER", port=gport, timeout_ms=500)
        ok3 = not r3["ok"] and "timeout" in r3.get("err", "") and time.time() - t0 < 3
        check("game (port)", ok1, r)
        check("game (port_file)", ok2, r2)
        check("game (timeout)", ok3, r3)

        # run
        r = A.call("run", exe="cmd.exe", args="/c echo hello winbot", wait_ms=20000, capture=True)
        check("run capture", r["ok"] and r.get("exit") == 0 and "hello winbot" in r.get("out", ""), r)
        r = A.call("run", exe="cmd.exe", args="/c echo [%WINBOT_X%][%OS%]& exit 7", wait_ms=20000, capture=True,
                   env=["WINBOT_X=v42", "os=WinbotOS"])
        check("run env (+ case-insensitive override) + exit code",
              r["ok"] and r.get("exit") == 7 and "[v42][WinbotOS]" in r.get("out", ""), r)
        r = A.call("run", exe=r"%SystemRoot%\system32\ping.exe", args="-n 60 127.0.0.1", wait_ms=0)
        ppid = r.get("pid")
        check("run wait 0", r["ok"] and isinstance(ppid, int) and "exit" not in r and "timeout" not in r, r)
        r = A.call("run", exe="ping.exe", args="-n 30 127.0.0.1", wait_ms=500)
        tpid = r.get("pid")
        check("run timeout", r["ok"] and r.get("timeout") is True, r)
        r = A.call("ps", name="PING.EXE")
        pids = [p["pid"] for p in r.get("procs", [])]
        r2 = A.call("ps", name="winagent.exe")
        me = [p for p in r2.get("procs", []) if p["pid"] == agent_pid]
        check("ps (name filter, case-insensitive)", r["ok"] and ppid in pids and tpid in pids, r)
        check("agent has no visible window", r2["ok"] and len(me) == 1 and me[0]["windows"] == [], r2)
        r = A.call("kill", pid=ppid)
        r3 = A.call("kill", pid=tpid)
        time.sleep(0.5)
        r2 = A.call("ps", name="ping.exe")
        left = [p["pid"] for p in r2.get("procs", []) if p["pid"] in (ppid, tpid)]
        check("kill", r["ok"] and r["killed"] == [ppid] and r3["ok"] and not left, (r, r3, r2))

        # second copy (the mutex) -> exit code 3
        r = A.call("run", exe=zpath(EXE), args="127.0.0.1:1 x 1 1", wait_ms=30000)
        check("second agent copy exits with code 3", r["ok"] and r.get("exit") == 3, r)

        # registry and user environment (test key / test name only)
        k = r"Software\winbot_test"
        r1 = A.call("reg_set", key=k, name="d", type="dword", value=42)
        r2 = A.call("reg_set", key=k, name="s", type="sz", value="héllo \U0001F600")
        g1 = A.call("reg_get", key=k, name="d")
        g2 = A.call("reg_get", key=k, name="s")
        g3 = A.call("reg_get", key=k, name="nope")
        check("reg_set", r1["ok"] and r2["ok"], (r1, r2))
        check("reg_get", g1.get("type") == "dword" and g1.get("value") == 42 and g2.get("type") == "sz"
              and g2.get("value") == "héllo \U0001F600" and g3["ok"] and g3["exists"] is False, (g1, g2, g3))
        r = A.call("env_user", name="WINBOT_TEST", value="v1")
        g = A.call("reg_get", key="Environment", name="WINBOT_TEST")
        check("env_user", r["ok"] and g.get("value") == "v1" and g.get("type") == "sz", (r, g))

        # input (DSMP uses the null graphics driver: nothing reaches the Mac screen)
        r = A.call("key", vk=0x87)                 # F24, bound to nothing
        check("key", r["ok"] or "SendInput" in r.get("err", ""), r)
        r = A.call("click", x=5, y=5)
        check("click", r["ok"] or "SetCursorPos" in r.get("err", "") or "SendInput" in r.get("err", ""), r)

        r = A.call("read", path=r"%TEMP%\winagent.log", offset=-20000)
        check("log file (%TEMP%\\winagent.log)", r["ok"] and "op ping" in unb64(r["b64"]), r.get("err"))

        # hub restart -> the agent reconnects
        port = hub.port
        hub.stop()
        time.sleep(1.5)
        hub.port = port
        hub.start()
        conns = hub.wait_conns(4, 20)
        ok = len(conns) == 4
        if ok:
            A = conns[0]
            ok = A.call("ping")["ok"]
        check("reconnect after hub restart", ok, len(conns))

        # exit
        r = A.call("exit")
        gone = False
        end = time.time() + 15
        while time.time() < end:
            if not (mac_agent_pids() - before):
                gone = True
                break
            time.sleep(0.3)
        check("exit", r["ok"] and gone, r)
    except Exception as e:
        check("test run", False, repr(e))
    finally:
        for p in mac_agent_pids() & mine:     # only the copy this test started
            try:
                os.kill(p, signal.SIGTERM)
            except OSError:
                pass
    n_fail = sum(1 for _, ok in RESULTS if not ok)
    print("%d checks, %d failed" % (len(RESULTS), n_fail))
    sys.exit(1 if n_fail else 0)


if __name__ == "__main__":
    main()
