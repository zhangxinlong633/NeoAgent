#!/usr/bin/env bash
# neo train_query：查询指定日期、出发/到达站的高铁/动车车次（12306 公开余票接口）。
# 仅读 NEO_TOOL_ARGS.{from,to,date}，不拼 shell。
#
# 修复要点：
#   - Cookie 必须用临时 jar 文件（-c/-b），禁止 -c -（会与 HTML 正文混在 stdout，导致握手假成功、查询非 JSON）。
#   - from/to 支持中文站名/城市名或电报码；站名经 station_name.js 映射。
#   - ERROR 打到 stdout（Neo 合并 stderr，但显式 stdout 更稳），失败非零退出，不伪造车次。
set -euo pipefail
exec python3 - <<'PY'
import json, os, re, subprocess, sys, tempfile, urllib.parse

raw = os.environ.get("NEO_TOOL_ARGS") or "{}"
try:
    obj = json.loads(raw)
except json.JSONDecodeError:
    print("ERROR: bad NEO_TOOL_ARGS JSON")
    sys.exit(2)

def get_str(key, maxlen=32):
    v = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(v, str) or not v.strip():
        print("ERROR: %s (string) required" % key)
        sys.exit(2)
    v = v.strip()
    if len(v) > maxlen:
        print("ERROR: %s too long" % key)
        sys.exit(2)
    return v

frm = get_str("from")
to = get_str("to")
date = get_str("date", 10)

if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date):
    print("ERROR: date must be YYYY-MM-DD")
    sys.exit(2)
for label, v in (("from", frm), ("to", to)):
    if not re.fullmatch(r"[0-9A-Za-z\u3400-\u9fff]+", v):
        print("ERROR: invalid %s characters" % label)
        sys.exit(2)

curl = "/usr/bin/curl" if os.path.isfile("/usr/bin/curl") else "curl"
UA = (
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"
)
BASE = "https://kyfw.12306.cn"

def run(args, timeout=25):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        print("ERROR: curl not found")
        sys.exit(2)
    except subprocess.TimeoutExpired:
        print("ERROR: 12306 request timed out")
        sys.exit(2)

def fail(msg, code=1):
    print("ERROR: " + msg)
    sys.exit(code)

tmpdir = tempfile.mkdtemp(prefix="neo-train-")
cj = os.path.join(tmpdir, "cj")
init_html = os.path.join(tmpdir, "init.html")
stations_js = os.path.join(tmpdir, "stations.js")
query_json = os.path.join(tmpdir, "query.json")

try:
    # 1) 握手：cookie 写入 jar，正文写入文件（互不污染）
    h = run([
        curl, "-fsSL", "--max-time", "20", "-A", UA,
        "-c", cj, "-o", init_html,
        BASE + "/otn/leftTicket/init",
    ])
    if h.returncode != 0:
        err = ((h.stderr or "").strip() or (h.stdout or "").strip())[:300]
        fail("12306 handshake failed: " + (err or ("exit %d" % h.returncode)))

    # 页面声明的查询路径，如 leftTicket/queryG
    query_path = "/otn/leftTicket/query"
    try:
        html = open(init_html, "r", encoding="utf-8", errors="replace").read()
    except OSError:
        html = ""
    m = re.search(r"CLeftTicketUrl\s*=\s*'([^']+)'", html)
    if m:
        rel = m.group(1).strip()
        if rel.startswith("leftTicket/"):
            query_path = "/otn/" + rel
        elif rel.startswith("/"):
            query_path = rel

    # 2) 站名表：电报码 <-> 站名
    s = run([
        curl, "-fsSL", "--max-time", "20", "-A", UA,
        "-b", cj, "-c", cj, "-o", stations_js,
        BASE + "/otn/resources/js/framework/station_name.js",
    ])
    if s.returncode != 0:
        err = ((s.stderr or "").strip() or (s.stdout or "").strip())[:300]
        fail("failed to fetch station list: " + (err or ("exit %d" % s.returncode)))

    try:
        js = open(stations_js, "r", encoding="utf-8", errors="replace").read()
    except OSError as e:
        fail("cannot read station list: " + str(e))

    # @id|站名|电报码|全拼|简拼|序号
    stations = []  # (name, code, pinyin, short)
    for part in js.split("@"):
        if not part.strip():
            continue
        f = part.split("|")
        if len(f) < 5:
            continue
        name, code, pinyin, short = f[1], f[2], f[3], f[4]
        if name and code:
            stations.append((name, code, pinyin.lower(), short.lower()))

    if not stations:
        fail("station list empty or format changed")

    def resolve(label, raw_name):
        """中文站名/城市、拼音或已是电报码 → 电报码。"""
        key = raw_name.strip()
        # 已是电报码（通常 3 大写字母）
        if re.fullmatch(r"[A-Z]{3}", key.upper()) and not re.search(r"[\u3400-\u9fff]", key):
            code_u = key.upper()
            for name, code, pinyin, short in stations:
                if code == code_u:
                    return code, name
            # 仍接受：可能表滞后
            return code_u, code_u

        key_l = key.lower()
        # 精确站名
        for name, code, pinyin, short in stations:
            if name == key:
                return code, name
        # 拼音 / 简拼精确
        for name, code, pinyin, short in stations:
            if pinyin == key_l or short == key_l:
                return code, name
        # 城市名：优先「同名站」，再「名站」常见南/北/西/东站中的主站启发式
        cands = [(name, code) for name, code, pinyin, short in stations if name.startswith(key)]
        if not cands:
            fail("%s: unknown station/city '%s' (use 中文站名 or telecode like NJH)" % (label, key))
        # 优先完全等于 key 的（上面已处理）；其次「key」本身作为站；再「key南」等常见
        preferred_suffix = ["", "南", "北", "东", "西", "站"]
        for suf in preferred_suffix:
            want = key + suf if suf != "站" else key
            for name, code in cands:
                if name == want or name == key + "站":
                    return code, name
        # 南京→南京(NJH) 在表里就是「南京」；若只有南京南等，取最短站名
        cands.sort(key=lambda x: (len(x[0]), x[0]))
        return cands[0][1], cands[0][0]

    from_code, from_name = resolve("from", frm)
    to_code, to_name = resolve("to", to)

    # 3) 查询余票
    q = urllib.parse.urlencode({
        "leftTicketDTO.train_date": date,
        "leftTicketDTO.from_station": from_code,
        "leftTicketDTO.to_station": to_code,
        "purpose_codes": "ADULT",
    })
    url = BASE + query_path + "?" + q
    r = run([
        curl, "-fsSL", "--max-time", "20", "-A", UA,
        "-b", cj, "-c", cj,
        "-H", "Referer: " + BASE + "/otn/leftTicket/init",
        "-o", query_json,
        url,
    ])
    if r.returncode != 0:
        err = ((r.stderr or "").strip() or (r.stdout or "").strip())[:300]
        fail("12306 query failed: " + (err or ("exit %d" % r.returncode)))

    try:
        body = open(query_json, "r", encoding="utf-8", errors="replace").read()
    except OSError as e:
        fail("cannot read query body: " + str(e))

    try:
        data = json.loads(body or "")
    except json.JSONDecodeError:
        fail("12306 returned non-JSON (blocked or API changed); body[:120]=" + repr((body or "")[:120]))

    rows = (data.get("data") or {}).get("result") or []
    if not rows:
        # 有时 data 为 null / 消息在 messages
        msgs = data.get("messages") or (data.get("data") or {}).get("message")
        extra = (" messages=" + str(msgs)[:160]) if msgs else ""
        fail("no trains found for %s %s(%s)->%s(%s)%s" % (
            date, from_name, from_code, to_name, to_code, extra))

    out = []
    for row in rows:
        f = row.split("|")
        if len(f) < 11:
            continue
        code = f[3]
        if not re.match(r"^[GDC]", code):
            continue
        # 字段：3=车次 8=发时 9=到时 10=历时
        out.append("%s %s->%s %s" % (code, f[8], f[9], f[10]))
        if len(out) >= 20:
            break

    if not out:
        fail("no G/D/C trains in %d raw rows (%s %s->%s)" % (
            len(rows), date, from_code, to_code))

    print("12306 %s %s(%s) -> %s(%s) G/D/C first %d:" % (
        date, from_name, from_code, to_name, to_code, len(out)))
    print("\n".join(out))
finally:
    for p in (cj, init_html, stations_js, query_json):
        try:
            os.remove(p)
        except OSError:
            pass
    try:
        os.rmdir(tmpdir)
    except OSError:
        pass
PY
