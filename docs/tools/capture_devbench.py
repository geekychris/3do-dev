"""Capture the annotated DevBench screenshots in docs/images/devbench/.

Needs Playwright with a Chrome install (no browser download):
    uv venv /tmp/pw && VIRTUAL_ENV=/tmp/pw uv pip install playwright
    ./3do build nova_defense && ./3do build arcade
    .venv/bin/tdo devbench --port 3331 &
    /tmp/pw/bin/python docs/tools/capture_devbench.py docs/images/devbench

Starts its own nova_defense and arcade sessions and stops them at the end.
"""
import json, sys, time, urllib.request
from playwright.sync_api import sync_playwright

B = "http://127.0.0.1:3331"
OUT = sys.argv[1]

def api(method, path, body=None):
    r = urllib.request.Request(B + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                               headers={"content-type": "application/json"})
    with urllib.request.urlopen(r, timeout=300) as resp:
        return json.loads(resp.read())

MARK_JS = """
([items]) => {
  document.querySelectorAll('.doc-mark').forEach(e => e.remove());
  /* sticky elements smear across full-page captures */
  document.querySelector('header').style.position = 'static';
  document.querySelectorAll('th').forEach(t => t.style.position = 'static');
  for (const [sel, n, dx, dy] of items) {
    const el = sel.startsWith('xpath=')
      ? document.evaluate(sel.slice(6), document, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null).singleNodeValue
      : document.querySelector(sel);
    if (!el) { console.log('missing ' + sel); continue; }
    const r = el.getBoundingClientRect();
    const m = document.createElement('div');
    m.className = 'doc-mark';
    m.textContent = n;
    m.style.cssText = `position:absolute;left:${r.left + window.scrollX + (dx||0) - 12}px;top:${r.top + window.scrollY + (dy||0) - 12}px;` +
      'width:24px;height:24px;border-radius:12px;background:#ff2d55;color:#fff;font:bold 14px/24px sans-serif;' +
      'text-align:center;z-index:9999;box-shadow:0 0 0 3px #fff,0 2px 8px #000a;pointer-events:none';
    document.body.appendChild(m);
    el.style.outline = '2px solid #ff2d55';
    el.style.outlineOffset = '2px';
    el.dataset.docOutlined = '1';
  }
}
"""
CLEAR_JS = """() => { document.querySelectorAll('.doc-mark').forEach(e => e.remove());
  document.querySelectorAll('[data-doc-outlined]').forEach(e => { e.style.outline=''; delete e.dataset.docOutlined; }); }"""

def shot(pg, name, marks, full=False, clip=None):
    pg.evaluate(MARK_JS, [marks])
    pg.wait_for_timeout(200)
    kw = {"path": f"{OUT}/{name}.png"}
    if full: kw["full_page"] = True
    if clip: kw["clip"] = clip
    pg.screenshot(**kw)
    pg.evaluate(CLEAR_JS)

# a game session for most views, the arcade for the snapshot story
nd = api("POST", "/api/sessions", {"target": "nova_defense"})["pid"]
api("POST", f"/api/sessions/{nd}/cmd/run_until", {"text": "NOVA: state=TITLE", "max_frames": 1500})
api("POST", f"/api/sessions/{nd}/cmd/step", {"frames": 30})
api("POST", f"/api/sessions/{nd}/cmd/press", {"buttons": "A"})
api("POST", f"/api/sessions/{nd}/cmd/hold", {"buttons": "LEFT"})
api("POST", f"/api/sessions/{nd}/cmd/step", {"frames": 120})
api("POST", f"/api/sessions/{nd}/cmd/release", {})
api("POST", f"/api/sessions/{nd}/select", {})

with sync_playwright() as p:
    br = p.chromium.launch(channel="chrome", headless=True)
    pg = br.new_page(viewport={"width": 1280, "height": 820})
    pg.goto(B + "/#Dashboard"); pg.wait_for_timeout(1500)
    pg.evaluate("loadSessions()"); pg.wait_for_timeout(1000)
    pg.select_option("#sessSel", str(nd)); pg.wait_for_timeout(1500)
    pg.click("nav >> text=Dashboard"); pg.wait_for_timeout(2500)
    shot(pg, "01-dashboard", [
        ("#sessSel", 1), ("header button", 2), ("#stCpu", 3),
        ("#sessTable", 4), ("#newTarget", 5), ("#summary", 6), ("#projTable", 7)], full=True)

    # screen: run it so the picture is live
    api("POST", f"/api/sessions/{nd}/cmd/run", {"speed": 1})
    pg.click("nav >> text=Screen"); pg.wait_for_timeout(3000)
    pg.check("#kbd")
    shot(pg, "02-screen", [("#screen", 1), (".pad", 2), ("#kbd", 3), ("#dev0", 4), ("#anlx", 5), ("#gifFrames", 6)], full=True)
    api("POST", f"/api/sessions/{nd}/cmd/pause", {})

    # OS: tasks, memory map, devices + detail
    pg.click("nav >> text=OS"); pg.click("#osNav >> text=Tasks"); pg.wait_for_timeout(1500)
    shot(pg, "03-os-tasks", [("#osNav", 1), ("#osBody table tr:nth-child(2) td:nth-child(3)", 2),
                             ("#osBody table tr:nth-child(1) th:nth-child(7)", 3), ("#osBody table tr:nth-child(1) th:nth-child(9)", 4),
                             ("#osBody table tr:nth-child(1) th:nth-child(10)", 5), ("#osFilter", 6), ("#osAuto", 7)],
         clip={"x": 0, "y": 0, "width": 1280, "height": 520})
    pg.click("#osNav >> text=Memory map"); pg.wait_for_timeout(1500)
    shot(pg, "04-os-memory-map", [("#osBody h4", 1), ("#osBody .memmap", 2), ("#osBody .legend", 3)],
         clip={"x": 0, "y": 0, "width": 1280, "height": 480})
    pg.click("#osNav >> text=Devices & I/O"); pg.wait_for_timeout(1500)
    pg.click("#osBody table tr.click >> nth=3"); pg.wait_for_timeout(1200)
    shot(pg, "05-os-devices-detail", [("#osBody h4", 1), ("#osBody h4:nth-of-type(3)", 2), ("#osDetail .kv", 3),
                                      ("#osDetail pre", 4), ("#osDetail a", 5)], full=True)

    # Debug: SWI snoop + profile while running, then a breakpoint stop
    pg.click("nav >> text=Debug"); pg.wait_for_timeout(800)
    api("POST", f"/api/sessions/{nd}/cmd/swi_trace", {"enable": True})
    api("POST", f"/api/sessions/{nd}/cmd/step", {"frames": 20})
    pg.click("#swi >> xpath=..//button[text()='fetch']"); pg.wait_for_timeout(1200)
    pg.fill("#profFrames", "150")
    pg.click("#prof >> xpath=..//button[text()='run']"); pg.wait_for_timeout(9000)
    pg.fill("#bpAddr", "draw_alien"); pg.click("text=+ break"); pg.wait_for_timeout(800)
    pg.click("text=Continue→stop"); pg.wait_for_timeout(3000)
    pg.fill("#symQ", "draw_"); pg.click("#symQ >> xpath=..//button[text()='search']"); pg.wait_for_timeout(1000)
    shot(pg, "06-debug", [("#stopBanner", 1), ("#regs", 2), ("#dis", 3), ("#bpAddr", 4), ("#syms", 5),
                          ("#swi", 6), ("#prof", 7)], full=True)
    api("POST", f"/api/sessions/{nd}/cmd/delete", {})
    api("POST", f"/api/sessions/{nd}/cmd/swi_trace", {"enable": False})
    api("POST", f"/api/sessions/{nd}/cmd/continue", {})

    # Memory: open the alien sprite data by symbol
    pg.click("nav >> text=Memory"); pg.wait_for_timeout(500)
    pg.fill("#memAddr", "sprite_a"); pg.fill("#memLen", "256"); pg.click("#memAddr >> xpath=..//button[text()='view']"); pg.wait_for_timeout(1000)
    pg.fill("#findTxt", "WAVE"); pg.click("text=find text"); pg.wait_for_timeout(1500)
    shot(pg, "07-memory", [("#memAddr", 1), ("#mem", 2), ("#findTxt", 3), ("#findOut", 4), ("#wAddr", 5)], full=True)

    # Console
    api("POST", f"/api/sessions/{nd}/cmd/press", {"buttons": "A"})
    api("POST", f"/api/sessions/{nd}/cmd/step", {"frames": 60})
    pg.click("nav >> text=Console"); pg.wait_for_timeout(2500)
    shot(pg, "08-console", [("#log", 1), ("#logFilter", 2), ("#events", 3)], clip={"x": 0, "y": 0, "width": 1280, "height": 760})

    # API explorer: call os_summary from the page
    pg.click("nav >> text=API"); pg.wait_for_timeout(1500)
    pg.fill("#apiFilter", "summary"); pg.wait_for_timeout(500)
    pg.click(".cmd:has(.name:text-is('os_summary')) button"); pg.wait_for_timeout(1500); pg.evaluate("window.scrollTo(0,0)")
    shot(pg, "09-api", [("#apiFilter", 1), ("xpath=//div[@class='cmd'][span[@class='name' and text()='os_summary']]/span[@class='name']", 2),
                        ("xpath=//div[@class='cmd'][span[@class='name' and text()='os_summary']]//span[contains(@class,'hint')]", 3), ("xpath=//div[@class='cmd'][span[@class='name' and text()='os_summary']]/pre", 4),
                        ("a[href='/api/openapi.json']", 5)], clip={"x": 0, "y": 0, "width": 1280, "height": 760})

    # Snapshot story on the arcade: snapshot in the menu, launch a game, diff
    ar = api("POST", "/api/sessions", {"target": "arcade"})["pid"]
    api("POST", f"/api/sessions/{ar}/cmd/run_until", {"text": "ARCADE: menu games=", "max_frames": 1500})
    api("POST", f"/api/sessions/{ar}/cmd/step", {"frames": 30})
    pg.evaluate("loadSessions()"); pg.wait_for_timeout(1500)
    pg.select_option("#sessSel", str(ar)); pg.wait_for_timeout(1500)
    pg.click("nav >> text=OS"); pg.click("#osNav >> text=Snapshots"); pg.wait_for_timeout(500)
    pg.fill("#snapName", "menu"); pg.click("text=take snapshot"); pg.wait_for_timeout(2000)
    api("POST", f"/api/sessions/{ar}/cmd/press", {"buttons": "A"})
    api("POST", f"/api/sessions/{ar}/cmd/run_until", {"text": "AMIGA3DO: ready ROCK", "max_frames": 900})
    api("POST", f"/api/sessions/{ar}/cmd/step", {"frames": 30})
    pg.click("text=diff vs now"); pg.wait_for_timeout(2000)
    pg.evaluate("document.getElementById('osDetailCard').style.display='none'")
    shot(pg, "10-os-snapshot-diff", [("#snapName", 1), ("#osBody .btns button:last-child", 2), ("#osBody .kv", 3),
                                     ("#osBody h4:nth-of-type(1)", 4), ("#osBody h4:nth-of-type(2)", 5)], full=True,
         clip={"x": 0, "y": 0, "width": 1280, "height": 900})
    br.close()

for pid in (nd, ar):
    api("DELETE", f"/api/sessions/{pid}")
print("done")
