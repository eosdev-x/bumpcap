# Decisions

## [2026-08-09] Project: Bumpcap
**By:** Tux
**Context:** Rewrite of bkw777/mainline for Fedora, C++17/Qt6, targeting Fedora + CachyOS kernels
**Decision:** Ground-up rewrite, not a fork. GPL-3.0. New project at bumpcap/

## [2026-08-09] Architecture: libbumpcap + thin frontends
**By:** Claude (Opus 5, from spec)
**Context:** Need CLI and GUI to share logic
**Decision:** Static core library (libbumpcap) linked by both bumpcap (GUI) and bumpcap-cli (CLI). No shared .so in v1.

## [2026-08-09] Package management: PackageKit primary, DNF CLI fallback
**By:** Claude (Opus 5, from spec)
**Context:** GUI never runs as root. Polkit for privilege escalation.
**Decision:** PackageKitBackend (D-Bus) as default. DnfCliBackend (pkexec dnf5) as fallback for headless/CLI. Auto-detect at startup.

## [2026-08-09] Privileged helper: D-Bus activated, Polkit-gated
**By:** Claude (Opus 5, from spec)
**Context:** COPR enable and grubby need root. PackageKit doesn't cover COPR.
**Decision:** bumpcap-helper as D-Bus system service with Polkit actions. Argument allow-listing for COPR (only bieszczaders/kernel-cachyos*).

## [2026-08-09] State: SQLite, Config: JSON
**By:** Claude (Opus 5, from spec)
**Context:** Pins, notes, install history need indexed queries. Config is naturally nested.
**Decision:** SQLite (QSqlDatabase) for state at ~/.local/share/bumpcap/state.db. JSON for config at ~/.config/bumpcap/config.json.

## [2026-08-09] Build system: CMake
**By:** Tux
**Decision:** CMake 3.21+ with AUTOMOC/AUTORCC/AUTOUIC. Qt 6.5+.
