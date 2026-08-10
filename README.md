# Bumpcap

Bumpcap is a Qt 6 desktop application for browsing, installing, and
managing Linux kernels on **Fedora**. It's a ground-up reimplementation of
the ideas behind [`mainline`](https://github.com/bkw777/mainline) (the
Ubuntu/GTK3/Vala kernel installer forked from `ukuu`), rebuilt in C++/Qt 6
and targeting Fedora's RPM/DNF/PackageKit ecosystem instead of `.deb`
packages.

## What it does

- Lists available kernels from **Fedora Stable**, **Fedora Rawhide**, and
  **CachyOS** (via the `bieszczaders/kernel-cachyos` COPR), showing
  install status, release date, and source for each.
- Installs and removes kernels through **PackageKit**/DNF — no raw
  `dpkg`-style file wrangling, and no GUI process ever runs as root.
- Supports kernel **pinning** (protect a kernel from removal) and
  per-kernel **notes**.
- Verifies SHA256 checksums of downloaded packages.
- Detects CPU microarchitecture (x86-64-v2/v3) to recommend the correct
  CachyOS variant.
- Integrates with **GRUB2/BLS** to show boot order, set the default
  kernel, and boot into a previous kernel for easy rollback.
- Sends desktop notifications when new kernels appear, with optional
  system tray integration for background awareness.
- Ships both a **GUI** (`bumpcap`) and a **CLI** (`bumpcap-cli`) built
  on a shared core library, so scripting/automation and interactive use
  stay in sync.

## Status

This repository currently contains the technical specification
([`SPEC.md`](./SPEC.md)) for Bumpcap v1. No code has been written yet —
the spec is intended to let a development team start implementation
without ambiguity around architecture, interfaces, data model, packaging,
or testing strategy.

## Tech stack

- **Language:** C++17
- **UI:** Qt 6 Widgets
- **Build:** CMake
- **Package management:** PackageKit (D-Bus) with a DNF CLI fallback
- **Privilege escalation:** Polkit, via a narrowly-scoped helper service
  for the few operations PackageKit doesn't cover natively (COPR repo
  management, `grubby` default-entry changes)
- **State/config:** JSON config file + SQLite state store
- **License:** GPL-3.0-or-later

## Read next

See [`SPEC.md`](./SPEC.md) for the full architecture, interface
definitions, UI mockups, data model, build/packaging instructions, and
testing strategy.
