#!/usr/bin/env python3
"""REQUIREMENTS-MANAGEMENT.md §4.3 — regenerate requirements/TRACE.md.

One specification status per REQ (its frontmatter `status`), one implementation
state per implementation (a cell), computed from the tags in that implementation's
tree and the steps that name it. The hand-kept prose (generation stamp, open
criteria, needs-reverify cells) comes from scripts/trace_cfg.py.

usage: python3 scripts/trace_gen.py [requirements/TRACE.md]
       (no argument: print the summary only, write nothing — the "coverage check")
"""
import glob
import importlib.util
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
OUT = sys.argv[1] if len(sys.argv) > 1 else None
_spec = importlib.util.spec_from_file_location("trace_cfg", os.path.join(ROOT, "scripts", "trace_cfg.py"))
cfg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cfg)

ID = re.compile(r"REQ-[A-Z]+-\d{3}")
SPEC_STATUSES = ("draft", "approved", "superseded", "rejected")


def frontmatter(path):
    txt = open(path, encoding="utf-8").read()
    m = re.match(r"---\n(.*?)\n---", txt, re.S)
    return m.group(1) if m else ""


def fm_field(fm, name):
    m = re.search(rf"^{name}:\s*(.*)$", fm, re.M)
    return m.group(1).split("#")[0].strip().strip('"') if m else ""


def fm_list(fm, name):
    raw = fm_field(fm, name)
    return re.findall(r'[A-Za-z0-9_.-]+', raw) if raw not in ("", "[]", "null") else []


# ---- implementations: every top-level directory with a src/ ----------------
IMPLS = sorted(d for d in os.listdir(".") if os.path.isdir(os.path.join(d, "src")))

# ---- 1. REQs ---------------------------------------------------------------
reqs = {}
for f in sorted(glob.glob("requirements/REQ-*.md")):
    fm = frontmatter(f)
    rid = fm_field(fm, "id")
    status = fm_field(fm, "status")
    reqs[rid] = dict(
        title=fm_field(fm, "title"), status=status, priority=fm_field(fm, "priority"),
        anchors=re.findall(r'"ARCHITECTURE\.md#([^"]+)"', fm),
        applies_to=[a for a in fm_list(fm, "applies_to") if a in IMPLS] or list(IMPLS),
        declared_applies=bool(fm_list(fm, "applies_to")), file=f)
    if status not in SPEC_STATUSES:
        print(f"WARNING {rid}: status '{status}' is not a specification status {SPEC_STATUSES}")

# ---- 2. steps --------------------------------------------------------------
steps = {}
for folder in ("todo", "doing", "done"):
    for f in sorted(glob.glob(f"workplan/{folder}/STEP-*.md")):
        fm = frontmatter(f)
        sid = fm_field(fm, "id")
        m = re.search(r"^  commits:\s*(.*)$", fm, re.M)
        steps[sid] = dict(
            folder=folder, implements=ID.findall(fm_field(fm, "implements")),
            impl=fm_field(fm, "implementation") or "all",
            cancelled=fm_field(fm, "cancelled") not in ("", "null"),
            commits_empty=bool(m) and m.group(1).split("#")[0].strip() == "[]")

# ---- 3. tags, per implementation --------------------------------------------
LEADER = re.compile(r"^\s*(\*/|\*|//|#)")
SKIP_DIRS = ("vendor", "__pycache__", "sdk", "build", "node_modules", ".git")


def files_under(paths):
    for p in paths:
        if os.path.isfile(p):
            yield p
        elif os.path.isdir(p):
            for dp, dn, fn in os.walk(p):
                dn[:] = sorted(d for d in dn if d not in SKIP_DIRS and not d.startswith("build"))
                for n in sorted(fn):
                    yield os.path.join(dp, n)


def ids_outside_parens(text, depth):
    ids, buf = [], ""
    for ch in text:
        if ch in "([":
            if depth == 0:
                ids += ID.findall(buf)
                buf = ""
            depth += 1
        elif ch in ")]":
            depth = max(0, depth - 1)
            buf = ""
        elif depth == 0:
            buf += ch
    if depth == 0:
        ids += ID.findall(buf)
    return ids, depth


def scan(paths, keywords):
    found = {}
    pat = re.compile(rf"\b({'|'.join(keywords)}):\s*(REQ-.*)$")
    for path in files_under(paths):
        try:
            lines = open(path, encoding="utf-8").read().splitlines()
        except (UnicodeDecodeError, OSError):
            continue
        for i, line in enumerate(lines):
            m = pat.search(line)
            if not m:
                continue
            ids, depth = ids_outside_parens(m.group(2), 0)
            if "*/" not in m.group(2):
                j = i + 1
                while j < len(lines):
                    nxt = lines[j]
                    s = nxt.strip()
                    if s in ("", "*", "#", "//", "*/") or not LEADER.match(nxt):
                        break
                    if re.search(rf"\b({'|'.join(keywords)}):", nxt):
                        break
                    more, depth = ids_outside_parens(LEADER.sub("", nxt), depth)
                    ids += more
                    if s.endswith("*/"):
                        break
                    j += 1
            for rid in dict.fromkeys(ids):
                found.setdefault(rid, []).append(f"{path}:{i + 1}")
    return found


code, tests = {}, {}
for impl in IMPLS:
    code_paths = [f"{impl}/src", f"{impl}/CMakeLists.txt", f"{impl}/cmake", f"{impl}/scripts",
                  f"{impl}/dev", f"{impl}/tests/CMakeLists.txt"]
    code_paths += sorted(glob.glob(f".github/workflows/{impl}-*.yml"))
    code[impl] = scan(code_paths, ["implements"])
    tests[impl] = scan([f"{impl}/tests"], ["verifies", "supports"])
    # a test-root implements tag counts as code too (gates are their own implementation)
    for rid, locs in scan([f"{impl}/tests"], ["implements"]).items():
        code[impl].setdefault(rid, []).extend(locs)

# ---- 4. anchors ------------------------------------------------------------
headings = {}
for line in open("ARCHITECTURE.md", encoding="utf-8"):
    m = re.match(r"^(#+)\s+(.*)$", line)
    if m:
        slug = re.sub(r"[^a-z0-9 -]", "", m.group(2).lower()).replace(" ", "-")
        headings[slug] = m.group(2)
broken = [(r, a) for r, d in reqs.items() for a in d["anchors"] if a not in headings]
for f in glob.glob("workplan/*/STEP-*.md"):
    for a in re.findall(r'"ARCHITECTURE\.md#([^"]+)"', frontmatter(f)):
        if a not in headings:
            broken.append((os.path.basename(f), a))
orphans = sorted(r for impl in IMPLS for r in set(code[impl]) | set(tests[impl]) if r not in reqs)
orphans += sorted(r for s in steps.values() for r in s["implements"] if r not in reqs)
orphans = sorted(set(orphans))

# ---- 5. cells --------------------------------------------------------------
REVERIFY = getattr(cfg, "REVERIFY", {})   # {"REQ-ID": {"c": "why"}}


def cell(rid, impl):
    d = reqs[rid]
    if d["status"] in ("superseded", "rejected") or impl not in d["applies_to"]:
        return "—"
    if impl in REVERIFY.get(rid, {}):
        return "needs-reverify"
    impl_steps = [s for s, sd in steps.items() if rid in sd["implements"] and not sd["cancelled"]
                  and sd["impl"] in (impl, "all")]
    all_done = all(steps[s]["folder"] == "done" for s in impl_steps)
    if rid in code[impl] and all_done:
        return "verified" if rid in tests[impl] else "implemented"
    if rid in code[impl] or rid in tests[impl]:
        return "in progress"
    return "open"


cells = {rid: {impl: cell(rid, impl) for impl in IMPLS} for rid in reqs}


def section(anchor):
    m = re.match(r"(\d+)-", anchor)
    return f"§{m.group(1)}" if m else anchor


counts = {s: sum(1 for d in reqs.values() if d["status"] == s) for s in SPEC_STATUSES}
cell_counts = {impl: {} for impl in IMPLS}
for impl in IMPLS:
    for rid in reqs:
        c = cells[rid][impl]
        cell_counts[impl][c] = cell_counts[impl].get(c, 0) + 1
n = {f: sum(1 for s in steps.values() if s["folder"] == f) for f in ("todo", "doing", "done")}
n_canc = sum(1 for s in steps.values() if s["cancelled"])
open_cells = [(rid, impl) for rid in sorted(reqs) for impl in IMPLS
              if reqs[rid]["status"] == "approved" and cells[rid][impl] == "open"]
unverified = [(rid, impl) for rid in sorted(reqs) for impl in IMPLS if cells[rid][impl] == "implemented"]
reverify_cells = [(rid, impl) for rid in sorted(reqs) for impl in IMPLS if cells[rid][impl] == "needs-reverify"]
empty_ev = sorted(s for s, sd in steps.items() if sd["folder"] == "done" and sd["commits_empty"])
draft_steps = sorted(s for s, sd in steps.items() if not sd["cancelled"] and sd["folder"] != "done"
                     and any(reqs.get(r, {}).get("status") == "draft" for r in sd["implements"]))

print(f"REQs {len(reqs)} {counts}; implementations {IMPLS}; cells {cell_counts}")
print(f"steps {len(steps)} (done {n['done']}, doing {n['doing']}, todo {n['todo']}, cancelled {n_canc})")
print("broken anchors:", broken or "none")
print("orphan tags / unknown REQ ids in steps:", orphans or "none")
print("open cells (approved REQ, no tag):", [f"{r}/{i}" for r, i in open_cells] or "none")
print("implemented without a tagged test:", [f"{r}/{i}" for r, i in unverified] or "none")
print("needs-reverify cells:", [f"{r}/{i}" for r, i in reverify_cells] or "none")
print("done steps with empty commits:", empty_ev or "none")
print("steps implementing draft REQs:", draft_steps or "none")
for rid in getattr(cfg, "SHOW", []):
    for impl in IMPLS:
        print(f"  {rid}/{impl}: {cells[rid][impl]} code={code[impl].get(rid)} tests={tests[impl].get(rid)}")
if OUT is None:
    sys.exit(0)

# ---- 6. render -------------------------------------------------------------
log = []
if os.path.exists(OUT):
    old_head = open(OUT, encoding="utf-8").read().split("\n| REQ |")[0]
    log = [p for p in old_head.split("\n\n") if p.startswith(("Generated:", "Previous:"))]
    if log:
        log[0] = "Previous:" + log[0][len("Generated:"):]
    log = log[:3]


def steps_of(rid, impl):
    return ", ".join(s.replace("STEP-", "") + f" ({steps[s]['folder']})" for s in sorted(steps)
                     if rid in steps[s]["implements"] and not steps[s]["cancelled"]
                     and steps[s]["impl"] in (impl, "all")) or "—"


head = "| REQ | Title | Status | Priority | Architecture | Applies to | " + " | ".join(
    f"{impl}: state | {impl}: steps | {impl}: code | {impl}: tests" for impl in IMPLS) + " |"
sep = "|" + "---|" * (6 + 4 * len(IMPLS))
rows = []
for rid in sorted(reqs):
    d = reqs[rid]
    arch = ", ".join(section(a) for a in d["anchors"]) or "—"
    applies = ", ".join(d["applies_to"]) if d["declared_applies"] else "all"
    row = f"| {rid} | {d['title'].replace('|', chr(92) + '|')} | {d['status']} | {d['priority']} | {arch} | {applies} |"
    for impl in IMPLS:
        row += (f" {cells[rid][impl]} | {steps_of(rid, impl)} | "
                f"{', '.join(code[impl].get(rid, [])) or '—'} | {', '.join(tests[impl].get(rid, [])) or '—'} |")
    rows.append(row)


def fmt(xs):
    return ", ".join(xs) if xs else "none"


scan_note = (
    f"Scan inputs: {len(reqs)} REQ files (`requirements/`), {len(steps)} step files "
    f"({n['done']} done, {n['doing']} doing, {n['todo']} todo, {n_canc} cancelled); implementations "
    f"{', '.join(IMPLS)} (every top-level directory with a `src/`). Per implementation, code tags "
    "(`implements: REQ-`) are scanned in `<impl>/src`, `<impl>/CMakeLists.txt`, `<impl>/cmake`, "
    "`<impl>/scripts`, `<impl>/dev`, `<impl>/tests/CMakeLists.txt` and `.github/workflows/<impl>-*.yml`; "
    "test tags (`verifies:`/`supports: REQ-`) in `<impl>/tests` (an `implements:` tag there counts as "
    "code — the gates implement their REQ). `sdk/`, `vendor/` and build trees are excluded. Multi-line "
    "tag blocks are read to the end of the comment paragraph; an ID inside a parenthetical is "
    "explanation, not a tag. A cell is `—` when the REQ is superseded/rejected or the implementation is "
    "outside its `applies_to`; `open` = no tag yet; `in progress` = tags but a step still open; "
    "`implemented` = tags and every step done; `verified` = implemented and a `verifies:` test exists."
)

report = [
    f"Totals: {len(reqs)} REQs — " + ", ".join(f"{counts[s]} {s}" for s in SPEC_STATUSES) + ". "
    + " ".join(f"Implementation `{impl}`: " + ", ".join(f"{v} {k}" for k, v in sorted(cell_counts[impl].items())) + "."
               for impl in IMPLS) + " " + getattr(cfg, "TOTALS_NOTE", ""),
    "",
    f"- **Approved REQs with an open cell (no tag in that implementation):** {fmt([f'{r}/{i}' for r, i in open_cells])}.",
    f"- **Implemented cells without a tagged test:** {fmt([f'{r}/{i}' for r, i in unverified])}. "
    + getattr(cfg, "UNVERIFIED_NOTE", ""),
    f"- **Orphan tags / unknown REQ ids in steps:** {fmt(orphans)}.",
    f"- **Broken architecture anchors:** {fmt([f'{r} → {a}' for r, a in broken])}.",
    f"- **`done/` steps with empty `evidence.commits`:** {fmt([s.replace('STEP-', '') for s in empty_ev])}.",
    "- **Deliberately open acceptance criteria:** " + getattr(cfg, "OPEN_CRITERIA", "none recorded."),
    f"- **needs-reverify cells:** {fmt([f'{r}/{i} — {REVERIFY[r][i]}' for r, i in reverify_cells])}.",
    f"- **Steps implementing `draft` REQs:** {fmt([s.replace('STEP-', '') for s in draft_steps])}.",
]

out = "\n".join([
    '<!-- GENERATED by REQUIREMENTS-MANAGEMENT.md §4.3 (scripts/trace_gen.py) — do not hand-edit. -->',
    "", "# Traceability matrix", "",
    cfg.STAMP, "", *(["\n\n".join(log), ""] if log else []), scan_note, "",
    head, sep, *rows, "", "## Coverage report", "", *report, "",
])
open(OUT, "w", encoding="utf-8").write(out)
print("wrote", OUT)
