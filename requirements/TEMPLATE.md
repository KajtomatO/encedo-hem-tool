---
id: REQ-XXXX-000              # REQ-<AREA>-<NNN>; area & numbering: REQUIREMENTS-MANAGEMENT.md §3.1
title: <one line, imperative or noun phrase>
status: draft                 # draft | approved | implemented | verified | needs-reverify | superseded | rejected
priority: must                # must | should | may  (RFC 2119)
revision: 1                   # bump on every edit; git history is the changelog
source: <user decision YYYY-MM-DD | ARCHITECTURE.md §n | external source per REQUIREMENTS-MANAGEMENT.md §8>
depends_on: []                # other REQ IDs this one builds on
supersedes: null              # REQ ID this replaces, if any
superseded_by: null           # filled when this REQ is superseded
traces:
  architecture: []            # list of "ARCHITECTURE.md#<anchor>" links (declared; see §4.1)
---

# <title>

<Normative statement. Exactly one SHALL / SHALL NOT / SHOULD / MAY. One
testable behavior — split if "and" joins two obligations.>

**Rationale:** <why this is required; cite conflicting sources explicitly.>

**Acceptance criteria:**
- [ ] <independently checkable criterion>
- [ ] <criterion — open "needs verification" items go here and stay unchecked>
