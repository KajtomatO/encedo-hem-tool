---
id: STEP-MX-000               # STEP-<Mx>-<NNN>; NNN in gaps of 10; never reused (§5.1)
title: <one line>
milestone: MX                 # from the Milestones section of ARCHITECTURE.md
implementation: c             # the implementation tree this step works in (c | js | python …); all = a shared chore
implements: []                # REQ IDs; may be [] for chores, justified in Notes
traces:
  architecture: []            # "ARCHITECTURE.md#<anchor>" links this step realizes
depends_on: []                # STEP IDs that must be in done/ first
evidence:                     # required (non-empty commits) before entering done/
  commits: []                 # short SHAs; commit subjects carry the "[STEP-…]" prefix
  tests: []                   # "verifies:" tags proven passing
  notes: null                 # e.g. why untestable, manual verification performed
reopened: []                  # append {date, reason} on each done/ -> todo/ move
cancelled: null               # set to a reason string to cancel (file stays in todo/)
---

**Goal:** <what exists and demonstrably works when this step is done>

**Notes:** <approach, gotchas, chore justification if implements is empty>

**Definition of done**
- [ ] <checkable outcome>
- [ ] <checkable outcome>
