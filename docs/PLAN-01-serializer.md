# Phase 1: Serializer Hardening & Schema Versioning
**Goal:** Solidify project load/save pipeline with comprehensive compatibility & forward-compat tests, add schema versioning helpers, document wire format.

**Context:** Current state has 4 test cases (91 passing assertions). Need to expand coverage for edge cases, add schema version helpers, and document the on-disk format to guide future phases.

---

## Tasks

### Task 1.1: Add Schema Version Helpers + Update Documentation
**Type:** Feature + Docs

**Action:**
1. Add `schema_version` constant to SceneSerializer.hpp (currently hard-coded as "0.2.0" in load/save)
2. Add `toVersion()` / `fromVersion()` helper functions for semver comparison
3. Create `docs/SCHEMA.md` documenting:
   - Wire format (JSON structure)
   - Version history (0.1.0 legacy → 0.2.0 current → future paths)
   - Backward/forward compatibility rules
   - Example: what changed from 0.1.0 → 0.2.0 (added materials, editor metadata)

**Files:**
- `engine/serial/include/SceneSerializer.hpp` — add constants & helpers
- `engine/serial/src/SceneSerializer.cpp` — implement helpers (or header-only if trivial)
- `docs/SCHEMA.md` — new file, comprehensive format doc

**Verify:**
- Helpers compile and have docstrings explaining semver logic
- Schema doc is readable by someone unfamiliar with the codebase
- Version constant is used in loadProject/saveProject

**Done:** Schema doc exists, version constant visible in code, helpers ready for use in tests

---

### Task 1.2: Add 3 More Compatibility Edge-Case Tests
**Type:** Test

**Action:**
Add to `tests/scene/test_serial_project.cpp` three new test cases:
1. **"Project with corrupted entity ID in face references loads with graceful fallback"** — save valid project, corrupt one face ref to point to nonexistent entity ID, reload and verify scene loads with the corrupt ref ignored
2. **"Project with missing required material texture falls back to default"** — create entity with material that references a texture ID that doesn't exist in materials, reload and verify entity loads with missing texture replaced by a sensible default (or white checkerboard placeholder)
3. **"Project with excessive nesting in properties loads without stack overflow"** — inject deeply nested property objects (10+ levels), verify load completes without crashing

**Files:**
- `tests/scene/test_serial_project.cpp` — add 3 new test cases

**Verify:**
- All 3 new tests run and pass
- Total test count: 7 (was 4), assertions > 110
- Catch2 output shows 7 passed

**Done:** New tests pass, test binary runs cleanly

---

### Task 1.3: Add Negative Tests (Intentional Failures)
**Type:** Test

**Action:**
Add to `tests/scene/test_serial_project.cpp` two negative test cases that verify load rejection:
1. **"Project with invalid JSON syntax fails to parse"** — write malformed JSON, attempt load, verify it returns an error (not crash/silent fail)
2. **"Project with missing critical schema fields fails with clear error message"** — remove `entities` or `version` field, reload, verify error contains field name

**Files:**
- `tests/scene/test_serial_project.cpp` — add 2 new negative tests

**Verify:**
- Both negative tests pass (i.e., load fails as expected with no crash)
- Error messages from loadProject are human-readable
- Test count: 9, assertions > 130

**Done:** Negative tests pass

---

## Verification Checklist
- [ ] `cmake --build build-tests --target test-serial` — 0 build errors
- [ ] `./build-tests/bin/test-serial` — 9 tests pass, all assertions pass, no crash
- [ ] `docs/SCHEMA.md` exists and is comprehensive
- [ ] `engine/serial/include/SceneSerializer.hpp` defines version constant
- [ ] Code compiles with `-Werror` if enabled, or at least no new warnings

## Success Criteria
- Phase 1 complete when:
  - 9 test cases pass (4 + 3 edge-case + 2 negative)
  - Schema documentation (SCHEMA.md) published
  - Version helpers are in place and documented
  - All builds clean with zero warnings

## Output (Summary)
After completion, update `/memories/session/phase-1-summary.md` with:
- Test count: 9, assertions passed
- Files modified: EditorApp.hpp, EditorApp.cpp, LevelValidator.hpp, LevelValidator.cpp, test_serial_project.cpp
- Files created: docs/SCHEMA.md
- Schema version constant value and location
- Any deviations from plan and why

---

**Ready to start?** Phase 1 will establish the data layer as rock-solid before we move to Editor UX.
