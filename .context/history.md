# History

## [2026-08-09] Project inception
**By:** Tux + Sonic
**Action:** Identified bkw777/mainline as inspiration, decided on C++17/Qt6 rewrite for Fedora

## [2026-08-09] Technical specification
**By:** Claude (Opus 5, ACP)
**Action:** Wrote comprehensive 1282-line SPEC.md covering architecture, interfaces, UI, data model, build system, packaging, testing
**Files:** bumpcap/SPEC.md, bumpcap/README.md

## [2026-08-09] Development started
**By:** Sonic (orchestrator), dev-skill pipeline
**Phase 1:** Foundation — CMake skeleton, model types, interfaces, core implementations

## [2026-08-09] Phase 4 review fixes
**By:** Tails (Codex subagent)
**Action:** Fixed Bumpcap security/code review blockers and warnings
**Details:** Routed bootloader and CachyOS COPR privileged operations through the helper with validated fallbacks, fixed DNF handle/progress tracking, made running/default kernel matching exact, tightened HTTP error handling, disconnected PackageKit transaction signals, removed broad helper signal policy, clarified COPR Polkit text, and locked state.db to owner read/write.
