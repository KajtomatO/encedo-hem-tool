# Requirements & Work Management (AI-Assisted)

How requirements and implementation work are managed for **encedo-hem-tool**
— the Encedo HEM device CLI, specified once and implemented per language.
This document has two audiences: **engineers** and **Claude Code**. Sections
labeled **PROCEDURE** are step-by-step instructions meant to be executed
literally by either. What is being built is defined in
[ARCHITECTURE.md](ARCHITECTURE.md); this document defines how the building is
tracked. It is the process of
[encedo-hem-c-api](https://github.com/KajtomatO/encedo-hem-c-api) carried
over (user decision 2026-10-27), with one addition: a requirement has
**one specification status** and **one implementation state per
implementation** (§3.2, §4.3).

## 1. Operating model

Division of labor — Claude produces, humans decide:

| Activity | Claude | Human |
|---|---|---|
| Draft / refine requirement text | does | reviews |
| Approve a requirement (draft → approved) | never | decides |
| Decompose a milestone into steps | proposes | approves before steps enter `todo/` |
| Implement a step, move it through folders | does, with evidence | may override |
| Compute an implementation's state for a REQ | does, only during trace regeneration, from evidence | may override |
| Reject / supersede a requirement | never | decides |
| Impact analysis on a change | produces the report, then stops | approves the consequences |

Two design rules govern all metadata in this process:

1. **Declare only what cannot be discovered; discover everything else.**
   Requirement files declare their architecture anchors and, when a
   requirement is not for every implementation, `applies_to` (judgment
   calls). Links to code, tests, and steps are *discovered* by scanning tags
   (§4.2) — never hand-listed, so they cannot go stale.
2. **Ground truth beats generated files.** `requirements/TRACE.md` is a
   report, not a database. If it disagrees with a fresh scan, it is stale —
   regenerate it (`python3 scripts/trace_gen.py requirements/TRACE.md`).

Claims about external systems must cite the ground-truth sources listed in
§8 or a test against the real system — never memory or plausibility.

## 2. Repository layout

```
requirements/
  TEMPLATE.md                    requirement template (copy to start a new REQ)
  REQ-<AREA>-<NNN>-<slug>.md     one file per requirement; never renamed, never deleted
  TRACE.md                       GENERATED traceability matrix — do not hand-edit
conformance/                     language-neutral vectors every implementation checks (REQ-TOOL-030)
workplan/
  TEMPLATE.md                    step template
  todo/                          approved steps, not started (freely editable here)
  doing/                         steps in progress (WIP limit: 2)
  done/                          completed steps (require evidence to enter)
scripts/trace_gen.py             the §4.3 generator (committed; its prose in scripts/trace_cfg.py)
<impl>/                          one directory per implementation: c/ today (js/, python/ … later),
                                 each a self-contained build with src/ and tests/
```

## 3. Requirements

### 3.1 File naming and format

`requirements/REQ-<AREA>-<NNN>-<slug>.md`

<!-- Area codes are permanent once used; adding a new area later is fine,
     renaming an existing one is not. -->

| Area | Covers |
|---|---|
| `TOOL` | the CLI's behaviour — commands, flags, output, exit codes, policies (the protected-key policy, the QR payload) — the shared specification every implementation realizes; ids `REQ-TOOL-001…028` came from encedo-hem-c-api at its 1.2.0 with their numbers and texts |
| `PKG` | how an implementation is built, tested, packaged and released: its SDK dependency and pin, CI legs, release assets, the public-API-only rule |

`NNN` is three digits, sequential within an area, and **never reused** — not
even for rejected or superseded requirements. The filename never changes after
creation. Frontmatter fields are defined by
[requirements/TEMPLATE.md](requirements/TEMPLATE.md); the body is: normative
statement first, then **Rationale**, then **Acceptance criteria** (each
criterion independently checkable).

**`applies_to`** (optional frontmatter list): the implementations a REQ is
for, e.g. `applies_to: ["c"]`. Absent = every implementation. Most `TOOL`
REQs apply to all; most `PKG` REQs name one.

### 3.2 Statuses and transitions

A requirement's `status` is the **specification's** lifecycle:
`draft → approved`, plus `superseded`, `rejected`. Whether an
implementation has realized and proven it is an **implementation state**,
computed per implementation during §4.3 from evidence and shown in the
TRACE matrix: `open` (no tag), `implemented`, `verified`, `needs-reverify`
(set by hand in the step, §6.2). The REQ file does not carry implementation
states.

| Transition | Who | Precondition |
|---|---|---|
| draft → approved | human only | — |
| any → superseded / rejected | human only | successor REQ referenced via `superseded_by` (for superseded) |
| cell open → implemented | Claude, during §4.3 only | ≥ 1 `implements:` tag in that implementation's code root **and** every step of that implementation whose `implements` lists this REQ is in `done/` |
| cell implemented → verified | Claude, during §4.3 only | ≥ 1 `verifies:` tagged test in that implementation's test root and the suite passes |
| cell any → needs-reverify | Claude, during §6.2 only | impact-analysis report presented; recorded in `scripts/trace_cfg.py` until the rework lands |
| cell needs-reverify → implemented/verified | Claude, during §4.3 | rework complete, evidence present again |

If a scan shows evidence has *disappeared* (a tag was deleted, a test
removed), Claude does not silently downgrade: it flags the cell loudly in the
coverage report.

### 3.3 Writing rules

- **Atomic**: one testable behavior per REQ. If "and" joins two obligations,
  split it.
- **RFC 2119**: SHALL / SHALL NOT / SHOULD / MAY, uppercase, exactly one
  SHALL-family keyword in the normative statement.
- **Language-neutral** for `TOOL`: name behaviour, not a library or a
  language construct; implementation detail goes to the `PKG` REQ of that
  implementation or to `applies_to`.
- **Cited**: `source` names where the requirement came from — a user decision
  with date, an `ARCHITECTURE.md` section, or an external source per §8.
- **Non-duplicative**: a REQ never restates another; use `depends_on`.
- **Edits**: any edit bumps `revision`; git history is the changelog. An edit
  that changes the *meaning* of an approved REQ resets `status` to `draft`
  (human must re-approve) and triggers the §6.2 procedure first — every
  implementation's cell is affected. Typo and rationale-only edits just bump
  `revision`.
- **Supersede / split**: create the new REQ(s), set the old one to
  `superseded` with `superseded_by` filled. History is never rewritten.

## 4. Traceability

### 4.1 Link model

```
ARCHITECTURE.md anchors ←declared— REQ —discovered→ code tags   (<impl>/ code root, per implementation)
                                    │ —discovered→ test tags   (<impl>/tests, per implementation)
                                    │ —discovered→ steps (frontmatter `implements` + `implementation`)
                                    └─ declared → depends_on (other REQs), applies_to (implementations)
STEP —discovered→ commits (subject prefix "[STEP-…]")
```

### 4.2 Tag conventions

| Where | Tag | Example |
|---|---|---|
| Code — `<impl>/src/`, `<impl>/CMakeLists.txt`, `<impl>/cmake/`, `<impl>/scripts/`, `<impl>/dev`, `.github/workflows/` | `implements: REQ-<ID>` in a comment at the primary implementation point(s) | `/* implements: REQ-TOOL-016 */` |
| Tests — `<impl>/tests/` | `verifies: REQ-<ID>` in a comment adjacent to the test case | `// verifies: REQ-TOOL-016` |
| Commits | subject starts with `[STEP-<ID>]` | `[STEP-T1-010] skeleton` |

A tag's implementation is the top-level directory it lives in; a tag under
`.github/workflows/` belongs to the implementation named in the workflow
file's name (`c-ci.yml` → `c`). Multiple REQ IDs may appear comma-separated
in one tag. Tag the few load-bearing points, not every touched line. The
greppable ground-truth patterns are `implements: REQ-` and `verifies: REQ-`.

### 4.3 PROCEDURE: regenerate the trace matrix

`python3 scripts/trace_gen.py requirements/TRACE.md` does steps 1–6; the
hand-kept prose (generation stamp, deliberately open criteria,
needs-reverify cells) lives in `scripts/trace_cfg.py` and is edited first.

1. Enumerate `requirements/REQ-*.md`; parse frontmatter (incl. `applies_to`).
   Enumerate the implementations: every top-level directory with a `src/`.
2. Enumerate `workplan/{todo,doing,done}/STEP-*.md`; parse `id`,
   `implements`, `implementation`, `cancelled`, and the folder.
3. Per implementation, grep its code root for `implements: REQ-` and its
   test root for `verifies: REQ-`; collect `file:line` per REQ.
4. Check every declared architecture anchor resolves to a heading in
   `ARCHITECTURE.md`.
5. Compute each cell (REQ × implementation) per §3.2; superseded/rejected
   REQs and implementations outside `applies_to` are `—`.
6. Overwrite `requirements/TRACE.md`: generation stamp, the matrix
   (REQ | title | status | priority | architecture | applies_to | one column
   per implementation with state, steps, code, tests), then the coverage
   report: approved REQs with an `open` cell; `implemented` cells without a
   passing tagged test; orphan tags; steps in `done/` with empty `evidence`;
   broken anchors; `needs-reverify` cells; draft REQs with steps.
7. Report the summary in chat.

**Read-only variant** ("coverage check"): run without the output path —
steps 1–5 + report only; no file written.

## 5. Implementation work (workplan)

### 5.1 Step files

`STEP-<Mx>-<NNN>-<slug>.md` — one file per step, living in exactly one of
`todo/`, `doing/`, `done/`. `Mx` is a milestone from the **Milestones**
section of `ARCHITECTURE.md` (`T1` = the first milestone of this repository;
each implementation's milestones are its own). `NNN` is numbered in gaps of
ten (010, 020, …) so insertions fit; IDs are never reused; the *filename*
never changes — only its folder does.

Frontmatter is defined by [workplan/TEMPLATE.md](workplan/TEMPLATE.md):
`id`, `title`, `milestone`, `implementation` (which tree the step works in,
or `all` for a shared chore), `implements` (REQ IDs), `traces.architecture`,
`depends_on` (step IDs), `evidence` (commits / tests / notes), `reopened`
(history), optional `cancelled`. Body: **Goal**, **Notes**, and a
**Definition of done** checklist.

Sizing: a step is at most ~one focused day and independently verifiable.
Steps SHOULD implement at least one approved REQ; pure chores (CI wiring,
docs) MAY carry `implements: []` with a one-line justification in Notes.

### 5.2 Status = folder location

- **`todo/`** — approved, not started. Files here may be freely edited or
  renumbered as understanding improves.
- **`doing/`** — in progress. WIP limit: at most **2** steps in `doing/`.
- **`done/`** — complete. A step may enter `done/` only when every
  Definition-of-done box is checked, `evidence.commits` is non-empty, and
  `evidence.tests` lists passing tags (or `evidence.notes` states why the
  step is untestable). Moves are `git mv` (followed by `git add` of the
  moved file, so later edits are staged); the move commit is the audit record.
- **Reopen**: `done/ → todo/` via `git mv`, appending
  `{date, reason}` to `reopened`. Prior evidence stays in the file.
- **Cancel**: a step that will never be done stays in `todo/` with
  `cancelled: <reason>` set; cancelled steps are excluded from coverage.
  Step files are never deleted.

### 5.3 PROCEDURE: decompose a milestone (rolling wave)

Trigger: a human asks ("decompose milestone T2"). Only one milestone is
decomposed in detail at a time.

1. Read the milestone's entry in the **Milestones** section of
   `ARCHITECTURE.md` and every architecture section it references.
2. Read all approved REQs; identify which this milestone realizes, for
   which implementation. If the milestone needs work that no approved REQ
   covers, draft the missing REQs first (§7) and get them approved — steps
   implement requirements, not improvisation (chore steps excepted).
3. Propose the step list as a table (id, title, implementation, implements,
   depends_on, rough size) in chat. The list ends with the milestone's
   **gate** step (a chore, `implements: []`), whose Definition of done
   always includes bringing the user-facing docs up to the milestone: the
   root `README.md` (the implementations table with versions and dates,
   Download), the implementation's `README.md`, `KNOWN-ISSUES.md`, and the
   implementation's `docs/RELEASING.md` "Final release" checklist. **Stop.**
4. On human approval: create the step files in `todo/` from the template,
   one commit (`[Tx] decomposition`).
5. Regenerate the trace matrix (§4.3).

### 5.4 PROCEDURE: implement a step

1. Preconditions: the step is in `todo/`, everything in its `depends_on` is
   in `done/`, WIP limit not exceeded, not `cancelled`.
2. `git mv` the file to `doing/` (+ `git add`); commit `[STEP-<ID>] start`.
3. Implement. Tag code `implements:` for each REQ in the step's
   `implements`; prefix every commit subject with `[STEP-<ID>]`.
4. Add or update tests tagged `verifies:` for those REQs; run them.
5. Fill `evidence`: commit SHAs, test tags, notes.
6. Check every Definition-of-done box (edit the file).
7. `git mv` to `done/` (+ `git add`); commit `[STEP-<ID>] done`.
8. Regenerate the trace matrix (§4.3) and report.

Commits are made only when the human permits; nothing is pushed by Claude;
tags and merges are the human's.

## 6. Change management & impact analysis

### 6.1 What counts as a change

- Editing the normative statement or acceptance criteria of an approved
  REQ.
- Editing `ARCHITECTURE.md` in a way that alters a decision or a section
  that REQs or steps trace to.
- Superseding or splitting a REQ.
- Changing a conformance vector (`conformance/`) — the REQ it pins changes
  with it.

Not a change in this sense: typo/rationale edits, adding a new independent
REQ, editing steps still in `todo/`.

### 6.2 PROCEDURE: impact analysis

Trigger: a human asks for it, **or Claude is about to make a §6.1 change —
in that case it stops and runs this first.**

1. Identify the changed items: REQ IDs and/or `ARCHITECTURE.md` anchors,
   from the edit or its diff.
2. Build the affected set:
   - REQs whose `traces.architecture` hit changed anchors;
   - REQs reachable from changed REQs via `depends_on` (transitively);
   - steps whose `implements` or `traces` hit the set — per implementation;
   - code and tests tagged with affected REQs (grep, per implementation);
   - which of those steps are in `done/`.
3. Produce the report: per affected item and implementation — what changed,
   why it is affected, proposed action (rework / re-verify / no action, with
   reason).
4. **Stop.** A human approves the report, possibly trimmed.
5. Apply the approved consequences: record the affected cells as
   `needs-reverify` (`scripts/trace_cfg.py`), bump the REQ's `revision`;
   reopen affected `done/` steps per §5.2 or create new steps; make the
   underlying edit itself.
6. After rework and passing tests, the next §4.3 run restores the cells;
   report closure in chat.

## 7. Drafting requirements (brief)

Claude drafts REQs from three sources: decisions made in conversation (cite
the date), `ARCHITECTURE.md`, and the external sources listed in §8. Drafts
always start at `status: draft` and are presented for approval — never
self-approved. Ambiguities, conflicts between sources, and anything the
architecture marks as *needs verification* are stated in the draft
explicitly (usually as an open acceptance criterion), not smoothed over.
When a user makes a decision in conversation that constrains implementation,
Claude SHOULD propose capturing it as a REQ on the spot.

## 8. AI limitations & guardrails (brief)

- **Citations for external-system behavior.** Claims about the systems,
  protocols, or APIs this project integrates with must cite one of the
  ground-truth sources below, or a test against the real system — never
  memory or plausibility. When sources conflict, name which source says
  what.

  | Source | Authoritative for |
  |---|---|
  | the real HEM device (dev machine) | final arbiter for all device behavior; wins over every document |
  | https://github.com/KajtomatO/encedo-hem-c-api | the C SDK the C implementation links: its public API (`include/ehem/*.h`, `docs/API-GUIDE.md`), its ARCHITECTURE and KNOWN-ISSUES (device quirks the SDK works around), its release packages |
  | Encedo Manager (implementation: `assets/build.js`) | the auth flow and the pairing QR payload the phone app expects (user decisions 2026-07-15, 2026-10-20) |
  | https://github.com/KajtomatO/encedo-hem-api-doc | HEM REST API endpoints, payloads, error codes |
  | https://github.com/KajtomatO/encedo-hem-python-api | `wipe_keys.py` — the protected-key policy's origin |

  Precedence when sources conflict: device > the SDK's live-pinned facts >
  Encedo Manager > API doc; conflicts are recorded in the affected REQ, not
  silently resolved.

- **No silent state changes.** Every status transition, folder move, or
  generated-file update appears in a commit and/or a chat report.
- **Uncertainty is propagated, not resolved by fiat.** Open questions stay
  visible as open acceptance criteria until actually verified.
- **Generated files are disposable.** `TRACE.md` must always be
  reproducible from a fresh scan; if it can't be, the scan wins.

## Appendix A: standing commands

| Say to Claude | Executes |
|---|---|
| "regenerate the trace matrix" | §4.3 |
| "requirements coverage check" | §4.3 read-only variant |
| "draft a requirement for \<topic\>" | §7 |
| "decompose milestone \<Tx\>" | §5.3 |
| "start step \<STEP-ID\>" | §5.4 steps 1–2 |
| "complete step \<STEP-ID\>" | §5.4 steps 4–8 |
| "impact analysis for \<REQ-ID / section\>" | §6.2 |
