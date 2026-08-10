# Bumpcap — Technical Specification

**Status:** Draft v0.1
**Author:** (assign)
**License:** GPL-3.0-or-later
**Target platform:** Fedora Linux (Workstation/KDE spins, x86_64 primary, aarch64 secondary)

---

## 1. Project Overview

### 1.1 Summary

Bumpcap is a desktop application for discovering, installing, pinning, and
managing Linux kernels on Fedora. It is a ground-up reimplementation of the
ideas in [`mainline`](https://github.com/bkw777/mainline) (itself a fork of
`ukuu`), which performs this role for Ubuntu using `.deb` packages fetched
from `kernel.ubuntu.com/mainline/`. Bumpcap targets Fedora's RPM-based
package ecosystem instead: Fedora's own repositories, Rawhide, and the
popular CachyOS-patched kernel builds distributed via COPR. Kernel.org
vanilla mainline support is scoped as a stretch goal (see §10) because
Fedora has no pre-built RPM stream for it comparable to Ubuntu's mainline
PPA.

### 1.2 Goals

- Give users one place to see what kernels are installed, available, and
  updatable across multiple sources.
- Make installing/removing/pinning kernels safe and GUI-driven, without
  requiring users to hand-edit `dnf.conf` exclude lists or run raw `dnf`
  commands.
- Integrate with GRUB so users can see and change boot order/default kernel
  and easily reboot into a previous kernel after a bad update.
- Do all privileged operations through PackageKit/Polkit — the GUI process
  itself never runs as root.
- Be a good Fedora citizen: use system package management (DNF/PackageKit)
  as the source of truth, never bypass RPM with manual file drops.

### 1.3 Non-goals (for v1)

- Not a general package manager. Scope is limited to `kernel*` packages
  (and, later, kernel.org vanilla builds).
- Not a distro-agnostic tool in v1. Fedora-only. Architecture should not
  preclude a Debian/Ubuntu backend later, but no such backend ships in v1.
- Not a kernel *builder*. Bumpcap installs pre-built packages; it does
  not run `make` / `rpmbuild` for the user in v1.

### 1.4 License

GPL-3.0-or-later, matching the upstream project this replaces. All new code
is original (C++/Qt), no Vala/GTK code is reused, but the license lineage
is kept for continuity and because it depends on GPL'd system libraries.

### 1.5 Naming

Working name: **Bumpcap**. Binary name: `bumpcap` (GUI),
`bumpcap-cli` (CLI). D-Bus service name:
`org.bumpcap.PrivilegedHelper` (only needed if a custom helper is used
instead of/in addition to PackageKit — see §4.4). Application ID for
desktop integration: `org.bumpcap.Bumpcap`.

---

## 2. Architecture

### 2.1 High-level design

Bumpcap is split into a **core library** (`libbumpcap`) with no Qt
Widgets dependency beyond Qt Core/Network/DBus, and two thin front ends:
a Qt Widgets **GUI** and a **CLI**. This separation lets the CLI and any
future systemd background monitor reuse the exact same source-fetching,
package-management, and state-persistence code as the GUI.

```
┌─────────────────────────────────────────────────────────────────┐
│                         bumpcap (GUI)                          │
│   Qt Widgets: MainWindow, SettingsDialog, DetailPanel, TrayIcon  │
└───────────────────────────────┬───────────────────────────────────┘
                                 │ uses
┌───────────────────────────────▼───────────────────────────────────┐
│                          libbumpcap                             │
│                                                                     │
│  ┌───────────────┐  ┌──────────────────┐  ┌─────────────────────┐ │
│  │ Kernel Source  │  │  Package Manager  │  │  Bootloader Manager │ │
│  │   Providers    │  │      Backend      │  │   (GRUB2 / BLS)     │ │
│  │ (strategy iface)│  │ (PackageKit/DNF5) │  │                     │ │
│  └───────┬────────┘  └─────────┬─────────┘  └──────────┬──────────┘ │
│          │                     │                        │           │
│  ┌───────▼─────────────────────▼────────────────────────▼────────┐ │
│  │                     Kernel Repository (in-memory model)         │ │
│  │        merges results from all sources into KernelInfo list     │ │
│  └───────────────────────────────┬─────────────────────────────────┘ │
│                                   │                                   │
│  ┌────────────────┐  ┌───────────▼──────────┐  ┌────────────────┐   │
│  │  ConfigManager  │  │   StateStore (SQLite  │  │  Notifier       │   │
│  │ (QSettings/JSON)│  │   or JSON, notes/pins)│  │ (libnotify/Qt)  │   │
│  └────────────────┘  └───────────────────────┘  └────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                                 │ uses
┌───────────────────────────────▼───────────────────────────────────┐
│                          bumpcap-cli                            │
│         argparse-based commands mirroring GUI actions              │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 Module breakdown

| Module | Namespace | Responsibility |
|---|---|---|
| `core/sources` | `kh::sources` | `IKernelSource` interface + `FedoraStableSource`, `FedoraRawhideSource`, `CachyOsCoprSource`, (future) `KernelOrgSource` |
| `core/pkg` | `kh::pkg` | `IPackageBackend` interface + `PackageKitBackend` (primary), `DnfCliBackend` (fallback) |
| `core/boot` | `kh::boot` | `IBootloaderManager` + `Grub2BlsManager` (Fedora uses BLS — Boot Loader Specification — on top of grub2) |
| `core/model` | `kh::model` | `KernelInfo`, `KernelSourceId`, `KernelStatus`, `InstalledKernelSet` value types |
| `core/repo` | `kh::repo` | `KernelRepository` — aggregates, dedupes, and cross-references source listings with installed state |
| `core/config` | `kh::config` | `ConfigManager` — reads/writes `~/.config/bumpcap/config.json` |
| `core/state` | `kh::state` | `StateStore` — persists pins, notes, last-seen versions, notification dedup, using SQLite via `QSqlDatabase` (driver `QSQLITE`) |
| `core/net` | `kh::net` | Thin wrapper over `QNetworkAccessManager` with retry/backoff, used by sources for HTTP(S) fetches |
| `core/notify` | `kh::notify` | `Notifier` — desktop notifications via the `org.freedesktop.Notifications` D-Bus interface (works with any compliant daemon, not tied to libnotify specifically) |
| `core/log` | `kh::log` | Logging façade wrapping `QLoggingCategory`, with categories per module |
| `gui/` | `kh::gui` | `MainWindow`, `SettingsDialog`, `DetailPanel`, `ProgressDialog`, `TrayIcon`, Qt models (`QAbstractTableModel` subclasses) |
| `cli/` | `kh::cli` | Command parser and command implementations (`list`, `install`, `remove`, `pin`, `unpin`, `set-default`, `check`) |
| `daemon/` (future) | `kh::daemon` | Optional systemd user service for periodic background checks + notifications, sharing `libbumpcap` |

### 2.3 Data flow (typical "refresh" operation)

1. GUI (or CLI `list`/`check`) calls `KernelRepository::refresh(sources)`.
2. `KernelRepository` asks each enabled `IKernelSource` to
   `fetchAvailable()` asynchronously (returns `QFuture<QList<KernelInfo>>`
   or emits a Qt signal on completion — see §3.2 for the exact interface).
3. In parallel, `KernelRepository` asks `IPackageBackend::installedKernels()`
   for what's actually on disk (from RPM database via PackageKit or
   `rpm -qa 'kernel*'`).
4. Results are merged: each `KernelInfo` gets a `KernelStatus` computed by
   comparing available vs. installed vs. running (`uname -r`) vs. pinned
   list (from `StateStore`).
5. `KernelRepository` emits `kernelListChanged(QList<KernelInfo>)`. GUI's
   table model updates; CLI prints and exits.
6. If new kernels appeared since the last recorded "seen" version per
   source (tracked in `StateStore`), `Notifier` fires a desktop
   notification (subject to settings).

### 2.4 Threading model

- All network I/O and PackageKit/DNF calls run off the GUI thread. Qt's
  `QNetworkAccessManager` is inherently async (signal-based) so no manual
  thread pool is required for HTTP fetches — sources issue requests and
  return futures/signals.
- Package installs/removals are long-running; these go through
  `QDBusPendingCallWatcher` against PackageKit's async D-Bus API, with
  progress signals (`PercentageChanged`, `StatusChanged`) marshalled back
  to the GUI thread via queued connections.
- `StateStore` (SQLite) access is confined to a single worker thread via a
  serial task queue to avoid `QSqlDatabase` multi-thread pitfalls (Qt SQL
  connections are not meant to be shared across threads).

### 2.5 Privilege separation

The GUI and CLI **never run as root**. All privileged actions (installing
packages, removing packages, enabling/disabling a COPR repo, regenerating
GRUB config, setting the default boot entry) are performed via:

- **PackageKit** (preferred) — handles install/remove/repo enable through
  its D-Bus API, which internally uses Polkit for authentication. This is
  the same mechanism GNOME Software / `pkcon` uses.
- **Polkit-invoked helper scripts** for the few operations PackageKit
  doesn't cover (COPR repo enable is a `dnf copr` operation, not native
  DNF; GRUB default-entry changes use `grubby`). Bumpcap ships a small
  set of Polkit actions (`org.bumpcap.policy`) and a minimal privileged
  helper binary (`bumpcap-helper`) invoked via `pkexec` or, preferably,
  registered as a D-Bus-activated Polkit-authenticated service so the user
  isn't shown a raw terminal-style pkexec prompt. See §4.4 for detail.

---

## 3. Kernel Source Interface

### 3.1 Design pattern

Strategy/plugin pattern: each kernel source implements a common interface.
`KernelRepository` holds a `QList<std::shared_ptr<IKernelSource>>` and
treats them uniformly. New sources (e.g., a future `KernelOrgSource`, or a
non-Fedora backend) are added by implementing the interface and registering
in `SourceFactory`, with no changes to `KernelRepository`, GUI table model,
or CLI required.

### 3.2 `IKernelSource` interface

```cpp
// core/sources/IKernelSource.h
#pragma once
#include <QObject>
#include <QList>
#include <QFuture>
#include "core/model/KernelInfo.h"

namespace kh::sources {

enum class SourceId {
    FedoraStable,
    FedoraRawhide,
    CachyOsStable,   // kernel-cachyos, x86-64-v3
    CachyOsLts,      // kernel-cachyos-lts, x86-64-v2
    KernelOrgMainline, // future
};

class IKernelSource : public QObject {
    Q_OBJECT
public:
    explicit IKernelSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IKernelSource() override = default;

    virtual SourceId id() const = 0;
    virtual QString displayName() const = 0;
    // Human-readable origin, e.g. "Fedora Updates repo" or
    // "COPR: bieszczaders/kernel-cachyos"
    virtual QString originDescription() const = 0;

    // True if this source requires enabling an extra repo (COPR) before
    // packages can be installed. Sources with true here must implement
    // ensureRepoEnabled().
    virtual bool requiresRepoSetup() const = 0;

    // Whether this source's packages are compatible with the running
    // machine (e.g. CachyOS v3 build requires x86-64-v3 CPU support).
    // Returns std::nullopt if not applicable/always compatible.
    virtual std::optional<CompatibilityResult> checkCompatibility() const = 0;

    // Kicks off an async fetch of currently-available kernel versions
    // from this source. Emits fetchFinished() or fetchFailed() when
    // done; never blocks the calling thread.
    virtual void fetchAvailable() = 0;

    // Idempotently enables whatever repo this source needs (e.g. runs
    // `dnf copr enable` via the privileged helper). No-op + immediate
    // success signal if requiresRepoSetup() is false or already enabled.
    virtual void ensureRepoEnabled() = 0;

signals:
    void fetchFinished(QList<kh::model::KernelInfo> kernels);
    void fetchFailed(QString errorMessage);
    void repoEnableFinished(bool success, QString errorMessage);
};

} // namespace kh::sources
```

`CompatibilityResult` is a small struct: `{ bool compatible; QString
reason; }`, e.g. `{false, "CPU lacks x86-64-v3 (AVX2) support; use
kernel-cachyos-lts instead"}`.

### 3.3 Concrete sources

#### 3.3.1 `FedoraStableSource`

- **Data origin:** Local DNF/RPM metadata — no bespoke HTTP scraping
  needed. Fedora already ships kernel updates through its standard
  repositories (`fedora`, `updates`, `updates-testing` if enabled).
- **Implementation:** Uses `IPackageBackend::checkAvailableUpdates("kernel*")`
  (backed by PackageKit's `GetUpdates`/`Resolve` or, as fallback,
  `dnf repoquery`) to list candidate `kernel`, `kernel-core`,
  `kernel-modules`, `kernel-modules-extra`, `kernel-devel`,
  `kernel-headers` package builds across enabled repos, then groups them
  by kernel version (the shared `%{version}-%{release}` across the
  sub-packages) into one `KernelInfo` per version.
- **Release date:** Taken from RPM `BUILDTIME` header (available via
  PackageKit package details / `rpm -q --qf '%{BUILDTIME}'`).
- **Changelog:** RPM `%changelog` via `PackageKit::GetDetails` /
  `rpm -q --changelog`.
- Does **not** require `requiresRepoSetup()`.

#### 3.3.2 `FedoraRawhideSource`

- Same mechanism as `FedoraStableSource`, but queries against the Rawhide
  repo. On a non-Rawhide install, this repo is not enabled by default;
  Bumpcap does **not** silently add a Rawhide repo file (too risky —
  Rawhide is a rolling, potentially-breaking release). Instead:
  - If the user's system is *already* Rawhide (`/etc/os-release` has
    `VARIANT_ID` / rawhide detection, or `$releasever` resolves to
    `rawhide`), query directly like the stable source.
  - If the user is on a stable release and enables this source in
    Settings, Bumpcap queries Rawhide's `repodata` over HTTP directly
    (read-only, via `core/net`) against
    `https://dl.fedoraproject.org/pub/fedora/linux/development/rawhide/Everything/x86_64/os/`
    without enabling a local DNF repo — for *browsing* only. Actually
    installing a Rawhide kernel onto a stable system is a heavyweight,
    unusual operation; Bumpcap surfaces a strong warning dialog
    ("Installing a Rawhide kernel is experimental and may not boot;
    are you sure?") and requires the user to explicitly opt in per
    install, still not persistently enabling the repo.
- **Changelog/date:** Same as stable, parsed from repodata `primary.xml`
  when read over HTTP, or from RPM headers when the repo is locally
  enabled (Rawhide-native systems).

#### 3.3.3 `CachyOsCoprSource` (parametrized for stable/LTS variant)

- **Data origin:** COPR project `bieszczaders/kernel-cachyos`.
  Two `SourceId`s share one implementation class, parametrized by variant:
  - `CachyOsStable` → package `kernel-cachyos` family, built for
    **x86-64-v3**.
  - `CachyOsLts` → package `kernel-cachyos-lts` family, built for
    **x86-64-v2**.
- **Repo metadata endpoint:** COPR exposes a repo file and repodata per
  chroot at
  `https://copr.fedorainfracloud.org/coprs/bieszczaders/kernel-cachyos/repo/fedora-$releasever/`.
  Before the repo is locally enabled, Bumpcap can query package
  versions via the **COPR REST API**
  (`https://copr.fedorainfracloud.org/api_3/package/list?ownername=bieszczaders&projectname=kernel-cachyos`)
  for "what's available" display without requiring repo setup — this lets
  the kernel list populate even for users who haven't opted in yet, with
  install still gated behind `ensureRepoEnabled()`.
- **`requiresRepoSetup()` → true.** `ensureRepoEnabled()` invokes the
  privileged helper to run the equivalent of
  `dnf copr enable bieszczaders/kernel-cachyos -y` (via `dnf5` COPR
  plugin D-Bus/CLI — see §4.4, since PackageKit has no native COPR
  concept).
- **Package grouping:** `kernel-cachyos`, `kernel-cachyos-core`,
  `kernel-cachyos-modules`, `kernel-cachyos-devel`,
  `kernel-cachyos-headers` (and `-lts` equivalents) are grouped into one
  logical `KernelInfo` per version, same as Fedora sources.
- **Compatibility check:** `checkCompatibility()` reads CPU feature flags
  (see §3.4) and returns incompatible + explanatory reason if:
  - Variant is `CachyOsStable` (v3) and CPU lacks AVX2/BMI2/FMA/etc.
    (x86-64-v3 baseline).
  - Neither variant is usable if CPU lacks x86-64-v2 baseline (rare on
    real hardware, mostly a concern in old VMs).
- **SHA256 verification:** COPR build results include per-RPM SHA256 in
  the repodata (`primary.xml` `<checksum>` per package). Bumpcap
  verifies downloaded RPM checksums against repodata before handing off
  to PackageKit/DNF for install (DNF/RPM already do this internally too,
  but Bumpcap performs an explicit pre-check consistent with the
  original `mainline` tool's checksum-verification feature, and to
  produce a clear failure message in the UI rather than a raw DNF error).

#### 3.3.4 `KernelOrgSource` (deferred, see §10)

- Interface stub only in v1: registered but disabled, greyed out in
  Settings with a tooltip explaining it's not yet implemented. See §10.1
  for the design sketch.

### 3.4 CPU architecture detection

`core/sources/CpuFeatures.h` provides a small utility used by
`CachyOsCoprSource::checkCompatibility()` and surfaced directly in the UI
(Settings → "System" tab shows detected microarchitecture level):

- Parse `/proc/cpuinfo` `flags` line, or preferably use `__builtin_cpu_supports`
  / `cpuid` via a tiny x86 feature-detection routine (GCC/Clang builtins:
  `__builtin_cpu_init()` + `__builtin_cpu_supports("x86-64-v3")` are
  available on modern GCC ≥ 11 / Clang ≥ 12 and are the preferred method —
  avoids hand-rolling CPUID parsing).
- Fallback: shell out to `/usr/bin/ld.so --help` on glibc ≥ 2.33, which
  prints supported "x86-64-v2/v3/v4" hwcaps subdirectories, or parse
  `/proc/cpuinfo` flags manually against known v2/v3/v4 requirement sets
  as a last resort for portability.
- Result cached in `ConfigManager` (rarely changes — recomputed on
  startup, not on every source refresh).

---

## 4. Package Management

### 4.1 Backend abstraction

```cpp
// core/pkg/IPackageBackend.h
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include "core/model/KernelInfo.h"

namespace kh::pkg {

struct OperationHandle {
    QString id;               // opaque, backend-specific (e.g. PackageKit transaction path)
};

class IPackageBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IPackageBackend() override = default;

    // Returns kernel-related packages currently installed (parsed rpm -qa
    // equivalent), grouped into KernelInfo entries with isInstalled=true.
    virtual void queryInstalled() = 0;

    // Kicks off an async install of all sub-packages that make up the
    // given kernel version from the given source's repo.
    virtual OperationHandle installKernel(const kh::model::KernelInfo &kernel) = 0;

    // Removes all sub-packages for the given installed kernel version.
    // Backend must refuse (emit operationFailed) if the target is the
    // currently-running kernel or is pinned, unless force=true.
    virtual OperationHandle removeKernel(const kh::model::KernelInfo &kernel, bool force = false) = 0;

    virtual void cancelOperation(const OperationHandle &handle) = 0;

signals:
    void installedQueryFinished(QList<kh::model::KernelInfo> installed);
    void installedQueryFailed(QString error);

    void operationProgress(OperationHandle handle, int percent, QString statusText);
    void operationFinished(OperationHandle handle, bool success);
    void operationFailed(OperationHandle handle, QString errorMessage);
};

} // namespace kh::pkg
```

Two implementations:

1. **`PackageKitBackend`** (default) — talks to `org.freedesktop.PackageKit`
   over D-Bus (system bus) using Qt's `QDBusInterface` /
   `QDBusPendingCall`, or the `PackageKitQt6` bindings if packaged for
   Fedora (`packagekit-qt6-devel`). Handles install/remove/query through
   PackageKit `Transaction` objects (`InstallPackages`, `RemovePackages`,
   `Resolve`, `GetUpdates`, `GetDetails`). Polkit prompts are shown by
   PackageKit's own agent (`polkit-gnome-authentication-agent-1` or
   desktop-provided equivalent) — Bumpcap does not need to render its
   own password dialog.
2. **`DnfCliBackend`** (fallback / CLI-mode default when no D-Bus session
   is available, e.g. over SSH) — shells out to `pkexec dnf5 ...` (or
   `dnf` if `dnf5` absent) with `QProcess`, parsing stdout for progress.
   Less ideal (parses text output, coarser progress reporting) but useful
   for headless/CLI usage and as a resilience fallback if PackageKit is
   unavailable/masked on a given system.

`ConfigManager` stores a `packageBackend` preference (`auto` / `packagekit`
/ `dnf-cli`); `auto` probes for the PackageKit D-Bus service at startup and
falls back to the CLI backend if absent.

### 4.2 Install flow

1. User selects a kernel row → clicks "Install".
2. If `source.requiresRepoSetup()` and repo not yet enabled → show a
   confirmation dialog explaining a COPR repo will be enabled system-wide,
   then call `ensureRepoEnabled()` (privileged helper, §4.4). Block
   further steps on its `repoEnableFinished` signal.
3. Compute the package set to install: for stock Fedora sources, this is
   `kernel`, `kernel-core`, `kernel-modules`, `kernel-modules-extra` at
   the target `evr` (epoch:version-release); for CachyOS,
   `kernel-cachyos[-lts]`, `-core`, `-modules`, optionally `-devel`/
   `-headers` if the user has "install headers/devel" enabled in Settings
   (default: off, since most users don't need them; DKMS users will want
   it on — expose as a per-source or global toggle).
4. `IPackageBackend::installKernel()` is called; `ProgressDialog` shown,
   driven by `operationProgress` signals.
5. On `operationFinished(success=true)`: refresh installed list, update
   `StateStore` (record install timestamp), show a success toast/status
   bar message, optionally prompt "Regenerate GRUB config now?" if the
   bootloader manager detects the new kernel isn't yet reflected in BLS
   entries (usually automatic via RPM `%posttrans` scriptlets on Fedora,
   but the prompt is a safety net — see §5).
6. On failure: show error dialog with the raw backend error plus a
   human-readable hint where recognizable (e.g. "not enough disk space in
   /boot" pattern-matched from common DNF transaction errors).

### 4.3 Remove flow

- Same backend call, `removeKernel()`.
- **Guardrails enforced in `libbumpcap`, not just the UI** (so CLI gets
  them too):
  - Refuse to remove the currently running kernel (`uname -r` match)
    unless `--force` (CLI) / explicit "this is unsafe" confirmation
    checkbox (GUI).
  - Refuse to remove a pinned kernel until unpinned first.
  - Warn (not block) if removing would leave fewer than 2 installed
    kernels (Fedora's own `installonly_limit`/`installonlypkgs` DNF
    behavior already prevents auto-removal below a floor during updates,
    but manual removal via Bumpcap should still nudge the user).

### 4.4 Privileged helper for non-PackageKit operations

Two operations aren't native PackageKit concepts and need their own
Polkit-gated path:

1. **COPR repo enable/disable** — `dnf5` has a `copr` plugin; there's no
   D-Bus API for it. Bumpcap installs:
   - A Polkit policy file `/usr/share/polkit-1/actions/org.bumpcap.policy`
     defining action `org.bumpcap.manage-copr` (default: `auth_admin`)
     and `org.bumpcap.set-default-kernel` (default: `auth_admin`).
   - A small privileged helper `bumpcap-helper`, a D-Bus system service
     (`org.bumpcap.Helper1`, activated on demand via
     `/etc/dbus-1/system.d/org.bumpcap.Helper1.conf` +
     `/usr/share/dbus-1/system-services/org.bumpcap.Helper1.service`),
     which checks the Polkit authorization for the calling process via
     `polkit-qt6` / `PolkitQt1::Authority` before executing:
     `dnf5 -y copr enable bieszczaders/kernel-cachyos`,
     `dnf5 -y copr disable bieszczaders/kernel-cachyos`, or
     `grubby --set-default <path>` (see §5).
   - This mirrors the well-established GNOME pattern (e.g.
     `system76-power`, `fwupd`) of a narrowly-scoped root helper rather
     than a general `pkexec <arbitrary command>` — the helper only
     accepts a small closed set of pre-validated operations with
     argument allow-listing (e.g. COPR project name must match
     `^[A-Za-z0-9_-]+/[A-Za-z0-9_-]+$` and be checked against a
     configured allow-list of known-safe projects, defaulting to just
     `bieszczaders/kernel-cachyos*`, to avoid the helper being usable as
     a confused deputy for enabling arbitrary third-party repos).
2. **`grubby --set-default`** — see §5.2.

---

## 5. Bootloader Integration

### 5.1 Fedora specifics

Fedora uses **GRUB2 + BLS (Boot Loader Specification)**: each kernel gets
a `.conf` snippet in `/boot/loader/entries/`, and `grub2-mkconfig` (or on
newer Fedora, `grub2-switch-to-blscfg` + BLS-aware grub) reads these
rather than a monolithic `grub.cfg` kernel list. RPM `%posttrans`
scriptlets for `kernel-core` packages already call `kernel-install` to
create/remove BLS entries automatically on install/remove — Bumpcap
does **not** need to hand-regenerate `grub.cfg` in the common case, unlike
Ubuntu where `mainline` explicitly ran `update-grub`.

### 5.2 `IBootloaderManager` interface

```cpp
// core/boot/IBootloaderManager.h
namespace kh::boot {

struct BootEntry {
    QString entryId;      // BLS entry filename, e.g. "6.10.5-200.fc40.x86_64"
    QString kernelVersion;
    QString title;
    bool isDefault;
    bool isCurrentlyRunning;
    int bootOrderIndex;   // position in `grubby --info=ALL` ordering
};

class IBootloaderManager : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void listBootEntries() = 0;             // -> bootEntriesListed(...)
    virtual void setDefaultEntry(const QString &entryId) = 0; // privileged
    virtual void regenerateConfig() = 0;             // privileged, rarely needed
    virtual void rebootIntoEntryOnce(const QString &entryId) = 0; // "boot this one, next boot only"

signals:
    void bootEntriesListed(QList<BootEntry> entries);
    void bootEntriesFailed(QString error);
    void operationFinished(bool success, QString error);
};

} // namespace kh::boot
```

- **`Grub2BlsManager`** implementation shells out to `grubby` (already
  present on every Fedora install; it's the standard tool Fedora itself
  uses to abstract BLS manipulation):
  - `listBootEntries()`: parses `grubby --info=ALL` (unprivileged, no
    helper needed — read-only).
  - `setDefaultEntry()`: privileged, routed through `bumpcap-helper`
    (`grubby --set-default <kernel-path>`).
  - `rebootIntoEntryOnce()`: privileged, uses `grub2-reboot
    <entry-index-or-id>` (equivalent to `grubby --set-default-index` for
    a single boot — sets `saved_entry` for next boot only via the
    `GRUB_DEFAULT=saved` mechanism Fedora ships by default), then offers
    to call `systemctl reboot` for the user (with explicit confirmation —
    a reboot is disruptive and must never be silent/automatic).
  - `regenerateConfig()`: privileged, runs `grub2-mkconfig -o
    /boot/grub2/grub.cfg` (or the BLS-aware no-op equivalent on systems
    already using `blscfg`) — exposed as an explicit "repair GRUB config"
    action for troubleshooting, not invoked automatically.

### 5.3 UI surface

The "Boot Order" panel (accessible from the main window toolbar) lists
`BootEntry` rows with columns: Order, Kernel Version, Title, Default?,
Running?. Buttons: "Set as Default", "Boot Once & Restart Now",
"Regenerate GRUB Config" (in an "Advanced" collapsible section, since it's
rarely needed on modern Fedora and mostly a repair tool).

---

## 6. UI Specification

### 6.1 Main Window

```
┌───────────────────────────────────────────────────────────────────────┐
│ Bumpcap                                                     _ □ x  │
├───────────────────────────────────────────────────────────────────────┤
│ File  Edit  View  Kernel  Tools  Help                                  │
├───────────────────────────────────────────────────────────────────────┤
│ [⟳ Refresh]  [⬇ Install]  [🗑 Remove]  [📌 Pin]  Filter: [________] ▾ │
├───────────────────────────────────────────────────────────────────────┤
│ ☐ │ Source        │ Version        │ Released   │ Status      │ Notes │
├───┼───────────────┼────────────────┼────────────┼─────────────┼───────┤
│   │ Fedora Stable │ 6.10.6-200.fc40│ 2025-08-01 │ ✓ Installed │       │
│   │               │                │            │  (running)  │       │
│ ☐ │ Fedora Stable │ 6.10.9-200.fc40│ 2025-08-06 │ ⬆ Update    │       │
│ ☐ │ Fedora Rawhide│ 6.16.0-0.rc3   │ 2025-08-05 │ Available   │       │
│ ☐ │ CachyOS       │ 6.10.7-1       │ 2025-08-04 │ Available   │ ★    │
│ ☐ │ CachyOS LTS   │ 6.6.45-1       │ 2025-08-03 │ ✓ Installed │       │
│                                                             📌 pinned │
├───────────────────────────────────────────────────────────────────────┤
│ Status: 5 kernels found · 1 update available · Last checked 09:14 AM  │
└───────────────────────────────────────────────────────────────────────┘
```

- Implemented as `QMainWindow` with a central `QTableView` backed by
  `KernelTableModel : QAbstractTableModel`.
- Columns: checkbox (multi-select for batch remove of old kernels),
  Source, Version, Released, Status, Notes (icon indicator, full text in
  tooltip/detail panel).
- Status column icons: ✓ installed, ✓ (running) for the active kernel, ⬆
  update available, plain text "Available" for not-installed, 📌 overlay
  badge for pinned.
- Row right-click context menu: Install / Remove / Pin / Unpin / Edit
  Note / Set as Default Boot / View Changelog / Copy Version.
- Double-click row → opens Detail Panel (§6.3) as a dock widget or modal,
  configurable in Settings (default: dock widget on the right, dismissible).
- Toolbar filter box does substring match across Source+Version; a
  dropdown next to it offers quick filters ("Installed only", "Updates
  only", "Pinned only").
- Column headers sortable (click to sort, click again to reverse),
  default sort: Version descending within each Source group, sources
  ordered by a user-configurable priority list (Settings).

### 6.2 Menus

- **File:** Refresh Now, Export Kernel List (CSV), Quit
- **Edit:** Preferences… (opens Settings dialog)
- **View:** Boot Order Panel, Detail Panel, Show/Hide status bar, Compact
  Mode (denser rows)
- **Kernel:** Install, Remove, Pin/Unpin, Set as Default, Edit Note,
  batch "Remove Old Kernels…" (opens a sub-dialog listing all
  non-running, non-pinned, non-latest-per-source kernels for one-click
  cleanup — directly inspired by `mainline`'s cleanup feature)
- **Tools:** Boot Order…, Regenerate GRUB Config, Open Log File, Check
  CPU Compatibility
- **Help:** About, Report Issue (opens configured issue tracker URL),
  Check for Bumpcap Updates

### 6.3 Detail Panel

Shown for the selected kernel row:

- Header: Source badge, full version string, release date, install
  status.
- **Changelog** tab: scrollable text (from RPM `%changelog` or COPR build
  logs where changelog isn't populated — COPR builds often lack rich
  changelogs, so this tab shows "No changelog available for this build;
  see upstream changes at <link>" gracefully rather than an empty box).
- **Files** tab (installed kernels only): list of packages + their file
  manifests (from PackageKit `GetFiles` or `rpm -ql`), grouped by
  sub-package (core/modules/devel/headers).
- **Dependencies** tab: what this kernel's packages `Requires` /
  `Provides`, useful for diagnosing DKMS module compatibility.
- **Notes** tab: free-text `QTextEdit`, autosaved to `StateStore` on
  focus-out/debounced, associated with the kernel's normalized version
  string (persists across reinstall/removal since it's keyed by version,
  not by an ephemeral package ID).

### 6.4 Settings Dialog

Tabs:

1. **Sources** — checkbox list: Fedora Stable (always on, disabled
   checkbox since it's the baseline), Fedora Rawhide, CachyOS (stable),
   CachyOS (LTS), Kernel.org Mainline (greyed out, "Coming soon" — see
   §10). Per-source priority/order (drag to reorder, affects default
   table sort and notification grouping). Inline compatibility warning
   next to CachyOS variants based on detected CPU (§3.4).
2. **Notifications** — enable/disable desktop notifications; per-source
   toggle; check interval (dropdown: 1h/6h/12h/daily/manual only); "only
   notify for stable, not Rawhide/pre-release" toggle.
3. **Updates & Install** — package backend (Auto/PackageKit/DNF CLI);
   "install headers+devel packages" toggle (default off); "confirm before
   every install/remove" toggle (default on); keep-N-old-kernels policy
   hint (informational — actual retention enforcement is DNF's
   `installonly_limit`, not duplicated here, but Bumpcap surfaces the
   current configured value read from `/etc/dnf/dnf.conf`).
4. **Bootloader** — default action after install (None / Prompt to set
   default / Always set newly installed kernel as default — last option
   off by default, since silently changing boot order is surprising).
5. **Advanced** — log level, log file location (with "Open Log Folder"
   button), reset all settings, clear cached kernel metadata.

### 6.5 Progress Dialog

Modal (but cancellable where the backend supports it) dialog shown during
install/remove/repo-enable operations:

- Title: operation description ("Installing kernel 6.10.9-200.fc40…")
- `QProgressBar` (indeterminate until PackageKit reports real percentage,
  then determinate)
- Status line: current sub-step ("Downloading kernel-core-6.10.9…",
  "Verifying checksum…", "Running post-install scripts…")
- Expandable "Details" section showing raw backend log lines (collapsed
  by default)
- Cancel button (disabled once past the point of safe cancellation, e.g.
  mid-RPM-transaction)

### 6.6 System Tray

- `QSystemTrayIcon`, optional (Settings toggle, default on if a tray/
  status-notifier host is detected on the desktop — GNOME needs an
  extension for this, KDE/XFCE support it natively; Bumpcap detects
  `QSystemTrayIcon::isSystemTrayAvailable()` and hides the option
  gracefully if unsupported rather than showing a broken icon).
- Icon changes to a badge/overlay when updates are available.
- Left-click: toggle main window visibility.
- Right-click menu: Refresh Now, Show Window, [list of up to 3 pending
  updates as quick-install items], Quit.
- Tray presence is what enables the "background monitoring" story without
  a separate daemon in v1 (see §9.4 for the systemd-service alternative
  considered for later).

### 6.7 CLI

Mirrors GUI actions for scripting/headless use:

```
bumpcap-cli list [--source=fedora-stable,cachyos,...] [--json]
bumpcap-cli check                       # refresh + print summary, exit code 1 if updates available (nagios-style)
bumpcap-cli install <version> [--source=...] [--yes]
bumpcap-cli remove <version> [--yes] [--force]
bumpcap-cli pin <version> / unpin <version>
bumpcap-cli note <version> "text"
bumpcap-cli boot-order [--set-default=<version>] [--boot-once=<version>]
bumpcap-cli notes-export / notes-import <file>   # for backup/sync
```

`--json` on any listing command emits machine-readable output (array of
the same fields as `KernelInfo`, see §7.1) for scripting/integration.

---

## 7. Data Model

### 7.1 `KernelInfo`

```cpp
// core/model/KernelInfo.h
namespace kh::model {

enum class KernelStatus {
    Available,       // exists at source, not installed
    Installed,       // installed, not currently running
    InstalledRunning,// installed and is `uname -r`
    UpdateAvailable,  // an installed-but-older version of this line has a newer build at the source
};

struct KernelInfo {
    kh::sources::SourceId sourceId;
    QString sourceDisplayName;     // denormalized for convenience/CLI --json
    QString version;               // full evr, e.g. "6.10.9-200.fc40.x86_64"
    QString shortVersion;          // "6.10.9" for grouping/sort
    QDateTime releaseDate;
    KernelStatus status;
    bool isPinned = false;
    QString notes;                 // last-known note snapshot (authoritative copy lives in StateStore)
    QStringList subPackages;       // e.g. ["kernel-core","kernel-modules",...]
    QString changelog;             // lazily populated on detail-view request, empty otherwise
    QString sha256;                // primary package checksum, when known from repodata
    std::optional<kh::sources::CompatibilityResult> compatibility;
};

} // namespace kh::model
```

`KernelInfo` is intentionally a plain value type (no Qt parent/ownership),
`Q_DECLARE_METATYPE`'d for use across signal/slot boundaries and
`QVariant`.

### 7.2 Configuration schema (`~/.config/bumpcap/config.json`)

Chosen over pure `QSettings`/INI because the source list, per-source
settings, and priority ordering are naturally nested/array structures
that are awkward in flat INI groups; `QSettings` with the `QJsonDocument`
custom format (or just direct JSON read/write) is used instead of
`QSettings`'s native format. `ConfigManager` wraps this so callers never
touch JSON directly.

```json
{
  "configVersion": 1,
  "sources": {
    "fedora-stable": { "enabled": true, "priority": 0 },
    "fedora-rawhide": { "enabled": false, "priority": 1 },
    "cachyos-stable": { "enabled": false, "priority": 2 },
    "cachyos-lts": { "enabled": false, "priority": 3 },
    "kernelorg-mainline": { "enabled": false, "priority": 4 }
  },
  "notifications": {
    "enabled": true,
    "checkIntervalMinutes": 360,
    "sourceOverrides": { "fedora-rawhide": false }
  },
  "install": {
    "backend": "auto",
    "installHeaders": false,
    "confirmBeforeInstallRemove": true
  },
  "bootloader": {
    "afterInstallAction": "prompt"
  },
  "ui": {
    "detailPanelMode": "dock",
    "compactRows": false,
    "columnOrder": ["source", "version", "released", "status", "notes"]
  },
  "advanced": {
    "logLevel": "info"
  }
}
```

### 7.3 State persistence (`~/.local/share/bumpcap/state.db`, SQLite)

Chosen over JSON for state (vs. config) because notes/pins/notification
history benefit from indexed lookup by version and from `StateStore`
being safely appendable/queryable without full-file rewrites, and because
it gives a clean upgrade path if usage history/analytics views are added
later.

```sql
CREATE TABLE pins (
    version TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    pinned_at INTEGER NOT NULL  -- unix epoch
);

CREATE TABLE notes (
    version TEXT PRIMARY KEY,
    note TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL
);

CREATE TABLE seen_versions (
    source_id TEXT NOT NULL,
    version TEXT NOT NULL,
    first_seen_at INTEGER NOT NULL,
    notified INTEGER NOT NULL DEFAULT 0, -- 0/1
    PRIMARY KEY (source_id, version)
);

CREATE TABLE install_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL,
    source_id TEXT NOT NULL,
    action TEXT NOT NULL,      -- 'install' | 'remove'
    performed_at INTEGER NOT NULL,
    success INTEGER NOT NULL
);

CREATE TABLE schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
); -- holds schema_version for migrations
```

`StateStore` applies migrations on startup by checking `schema_meta` and
running any pending numbered migration (`migrations/0001_init.sql`, …)
via `QSqlQuery`, keeping the pattern extensible without an external ORM
dependency.

---

## 8. Build System

### 8.1 Dependencies

| Dependency | Fedora package | Purpose |
|---|---|---|
| Qt 6 (Core, Widgets, Network, DBus, Sql, Concurrent) | `qt6-qtbase-devel` | Core framework |
| CMake ≥ 3.21 | `cmake` | Build system |
| PackageKit-Qt6 | `packagekit-qt6-devel` | Package operations |
| polkit-qt6-1 | `polkit-qt6-1-devel` | Polkit auth checks in helper |
| sqlite | `sqlite-devel` (via Qt Sql plugin `qt6-qtbase-sqlite` / bundled) | State store |
| GTest/GMock (dev only) | `gtest-devel` | Unit tests |
| rpm-devel (optional, if direct librpm queries used instead of shelling to `rpm`) | `rpm-devel` | RPM metadata queries |
| desktop-file-utils, appstream | build-time only | `.desktop`/AppStream validation |

### 8.2 Repository layout

```
bumpcap/
├── CMakeLists.txt                 # top-level, options, find_package calls
├── cmake/
│   └── modules/                   # any custom Find*.cmake if needed
├── core/
│   ├── CMakeLists.txt             # builds libbumpcap (STATIC or SHARED, default STATIC)
│   ├── sources/  (headers+.cpp)
│   ├── pkg/
│   ├── boot/
│   ├── model/
│   ├── repo/
│   ├── config/
│   ├── state/
│   │   └── migrations/*.sql (embedded via CMake qt_add_resources)
│   ├── net/
│   ├── notify/
│   └── log/
├── gui/
│   ├── CMakeLists.txt             # builds bumpcap binary
│   ├── MainWindow.{h,cpp,ui or built programmatically}
│   ├── SettingsDialog.*
│   ├── DetailPanel.*
│   ├── ProgressDialog.*
│   ├── TrayIcon.*
│   ├── models/KernelTableModel.*
│   └── resources/ (icons, .qrc)
├── cli/
│   ├── CMakeLists.txt             # builds bumpcap-cli binary
│   └── main.cpp, commands/*
├── helper/
│   ├── CMakeLists.txt             # builds bumpcap-helper (privileged D-Bus service)
│   ├── main.cpp
│   └── org.bumpcap.Helper1.{service,conf}, org.bumpcap.policy
├── packaging/
│   ├── bumpcap.spec
│   ├── org.bumpcap.Bumpcap.desktop
│   ├── org.bumpcap.Bumpcap.appdata.xml
│   └── icons/
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/                      # GTest-based, one binary per module roughly
│   └── integration/                # scripted, may require a Fedora container
├── docs/
│   └── (developer docs, this SPEC.md's home in-tree as docs/SPEC.md too, optional)
├── LICENSE                        # GPL-3.0
└── README.md
```

### 8.3 Top-level `CMakeLists.txt` structure (outline)

```cmake
cmake_minimum_required(VERSION 3.21)
project(Bumpcap VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(KERNELHUB_BUILD_TESTS "Build unit/integration tests" ON)
option(KERNELHUB_BUILD_GUI "Build the Qt Widgets GUI" ON)
option(KERNELHUB_BUILD_CLI "Build the CLI" ON)
option(KERNELHUB_BUILD_HELPER "Build the privileged D-Bus helper" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network DBus Sql Concurrent)
find_package(PackageKitQt6 REQUIRED)
find_package(PolkitQt6-1 REQUIRED)

add_subdirectory(core)
if(KERNELHUB_BUILD_GUI)
    add_subdirectory(gui)
endif()
if(KERNELHUB_BUILD_CLI)
    add_subdirectory(cli)
endif()
if(KERNELHUB_BUILD_HELPER)
    add_subdirectory(helper)
endif()
if(KERNELHUB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES packaging/org.bumpcap.Bumpcap.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/org.bumpcap.Bumpcap.appdata.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
```

`core/CMakeLists.txt` builds `libbumpcap` as a static library linked
into both `bumpcap` and `bumpcap-cli`, keeping a single source of
truth and avoiding runtime `.so` versioning concerns for what is
currently an internal-only library (not a public API in v1 — revisit if
a daemon needs to link it as a shared lib later).

### 8.4 Build instructions (for README/dev docs)

```bash
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel \
    packagekit-qt6-devel polkit-qt6-1-devel sqlite-devel gtest-devel

git clone https://github.com/<org>/bumpcap.git
cd bumpcap
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

---

## 9. Packaging

### 9.1 RPM spec outline (`packaging/bumpcap.spec`)

```spec
Name:           bumpcap
Version:        0.1.0
Release:        1%{?dist}
Summary:        Browse, install, and manage Linux kernels on Fedora
License:        GPL-3.0-or-later
URL:            https://github.com/<org>/bumpcap
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  packagekit-qt6-devel
BuildRequires:  polkit-qt6-1-devel
BuildRequires:  sqlite-devel
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

Requires:       qt6-qtbase%{?_isa}
Requires:       PackageKit
Requires:       polkit
Requires:       grubby
Requires:       %{name}-helper = %{version}-%{release}

%description
Bumpcap is a graphical and command-line tool for browsing, installing,
pinning, and managing Linux kernels on Fedora from Fedora's own
repositories, Rawhide, and CachyOS-patched builds distributed via COPR.

%package cli
Summary:        Command-line interface for Bumpcap
Requires:       %{name} = %{version}-%{release}
%description cli
Headless/scriptable CLI for Bumpcap's kernel management features.

%package helper
Summary:        Privileged D-Bus helper for Bumpcap
Requires:       polkit
%description helper
Polkit-mediated helper performing privileged operations (COPR repo
management, GRUB default entry changes) on behalf of Bumpcap.

%prep
%autosetup

%build
%cmake -DKERNELHUB_BUILD_TESTS=OFF
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/org.bumpcap.Bumpcap.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/org.bumpcap.Bumpcap.appdata.xml

%files
%license LICENSE
%doc README.md
%{_bindir}/bumpcap
%{_datadir}/applications/org.bumpcap.Bumpcap.desktop
%{_datadir}/metainfo/org.bumpcap.Bumpcap.appdata.xml
%{_datadir}/icons/hicolor/*/apps/org.bumpcap.Bumpcap.*

%files cli
%{_bindir}/bumpcap-cli

%files helper
%{_libexecdir}/bumpcap/bumpcap-helper
%{_datadir}/dbus-1/system-services/org.bumpcap.Helper1.service
%{_sysconfdir}/dbus-1/system.d/org.bumpcap.Helper1.conf
%{_datadir}/polkit-1/actions/org.bumpcap.policy

%changelog
* Mon Aug 09 2026 Your Name <you@example.com> - 0.1.0-1
- Initial package
```

### 9.2 Distribution path

- Primary target: submission to **Fedora COPR** first (fast iteration,
  matches how CachyOS kernels themselves are distributed — a familiar
  install path: `dnf copr enable <org>/bumpcap`), with an eye toward
  eventual Fedora package review / inclusion in official repos once
  stable.
- AppStream metadata (`org.bumpcap.Bumpcap.appdata.xml`) included
  from v1 so the app shows up correctly in GNOME Software / KDE Discover
  once packaged, with screenshots, description, and the GPL-3.0 license
  tag.

---

## 10. Testing Strategy

### 10.1 Unit tests (GTest, `tests/unit/`)

- **Model layer:** `KernelInfo` grouping/sorting logic, `KernelStatus`
  derivation given synthetic (available, installed, running, pinned)
  combinations — pure functions, no I/O, high coverage expected here.
- **Source parsers:** Feed canned repodata XML / COPR API JSON fixtures
  (checked into `tests/fixtures/`) into each `IKernelSource`
  implementation's parsing logic (refactored so parsing is a
  separately-testable pure function from the actual network fetch) and
  assert correct `KernelInfo` extraction, including edge cases: missing
  changelog, malformed version strings, duplicate sub-packages.
- **CPU feature detection:** Mock `/proc/cpuinfo` content, assert
  correct v2/v3 classification.
- **ConfigManager/StateStore:** Round-trip serialization tests; SQLite
  migration tests (apply migrations 1→N against a fixture DB, assert
  final schema and data integrity); JSON config backward-compatibility
  tests (old config version + missing fields → sane defaults, no crash).
- **Package backend guardrails:** Unit-test the *policy* logic (refuse to
  remove running/pinned kernel) against a fake `IPackageBackend`, without
  needing a real PackageKit/DNF present — this is the most important
  correctness surface to lock down since it's a safety feature.

### 10.2 Integration tests (`tests/integration/`)

- Run inside a disposable Fedora container/VM (documented as requiring
  `podman` or similar; not run in default `ctest` unless
  `KERNELHUB_INTEGRATION_TESTS=ON`, since they need root and network):
  - Real PackageKit install/remove of a *non-kernel* small test package
    to validate the `PackageKitBackend` D-Bus plumbing without the risk
    of touching the container's actual bootable kernel.
  - COPR repo enable via `bumpcap-helper` against the real
    `bieszczaders/kernel-cachyos` COPR (network-dependent, marked
    `@network` and skippable in CI without network egress).
  - `grubby --info=ALL` parsing against a real Fedora `/boot/loader/entries`
    layout (can run in a container with a synthetic BLS entries directory
    rather than requiring an actual bootable installation).
- CI (GitHub Actions or equivalent) matrix: build on latest 2 Fedora
  releases + Rawhide container images, run unit tests always, run
  integration tests on a nightly/manual trigger only (not blocking PRs,
  due to network/root requirements and flakiness risk).

### 10.3 Manual test plan (pre-release checklist)

1. Fresh Fedora Workstation VM, default repos only:
   - Launch app, verify Fedora Stable list populates and matches
     `dnf list --showduplicates kernel-core`.
   - Install an available update kernel; verify BLS entry appears
     (`grubby --info=ALL`), reboot, confirm new kernel boots.
   - Pin the previous kernel; attempt removal via CLI without `--force`;
     confirm refusal with clear error message.
   - Set a non-running kernel as default boot entry; reboot; confirm it
     boots.
2. Enable Rawhide source on a stable system: confirm warning dialog
   appears before any Rawhide install; cancel and confirm no repo was
   left enabled afterward.
3. Enable CachyOS source on a CPU without AVX2 (or a VM configured to
   hide it via `-cpu` qemu flags): confirm `CachyOsStable` is flagged
   incompatible and install is blocked/warned, while `CachyOsLts`
   proceeds normally.
4. Install CachyOS kernel on a compatible machine: confirm COPR gets
   enabled (visible in `dnf copr list`), checksum verification message
   appears in progress dialog, kernel boots successfully afterward.
5. Notification flow: lower check interval to 1 minute in Settings
   (test-only convenience, not a real preset), confirm a desktop
   notification appears exactly once per newly-seen version (not
   repeated on every check).
6. Remove old kernels via "Kernel → Remove Old Kernels…" batch dialog;
   confirm running + pinned kernels are excluded from the candidate list
   automatically.
7. Kill network mid-download; confirm PackageKit-driven install fails
   gracefully with a readable error, no partial/corrupt state left in
   `StateStore`, and a retry works cleanly afterward.
8. CLI smoke test: `bumpcap-cli list --json | jq` produces valid JSON;
   `bumpcap-cli check` exit code reflects update availability
   (scriptable in a cron/systemd-timer context).
9. Accessibility/basic UX pass: keyboard-only navigation of the main
   table and dialogs; verify tab order and that all destructive actions
   (remove, set default, reboot) require an explicit confirm step.
10. Uninstall via `dnf remove bumpcap bumpcap-cli bumpcap-helper`:
    confirm Polkit policy files, D-Bus service files, and desktop/
    AppStream entries are all cleanly removed (nothing orphaned outside
    RPM's tracked file list); user config/state under `~/.config` and
    `~/.local/share` intentionally left behind per XDG convention (not
    an RPM concern).

---

## 11. Future Extensions

### 11.1 Kernel.org vanilla mainline builds

Deferred from v1 because Fedora has no first-party pre-built RPM stream
analogous to Ubuntu's `kernel.ubuntu.com/mainline` PPA-style builds.
Options for a future `KernelOrgSource`, roughly in increasing complexity:

1. **Track a known community COPR that rebuilds mainline/RC kernels for
   Fedora** (if/when one exists with reasonable trust and maintenance —
   would need vetting before being bundled as a default source; likely
   ships disabled-by-default with a clear "third-party, unofficial"
   label, same trust posture as CachyOS but with an extra warning since
   it's literally upstream RC code).
2. **Bumpcap-maintained COPR**: the project itself maintains a COPR
   project that rebuilds tagged kernel.org releases (stable + RC) as
   Fedora RPMs using Fedora's own kernel `.spec` as a base with the
   version bumped — most control, most maintenance burden (essentially
   becomes a downstream kernel-packaging project).
3. **Local build mode**: Bumpcap orchestrates `fedpkg`/`rpmbuild`
   locally against a fetched kernel.org tarball + Fedora's kernel spec/
   config, with a progress UI for the (very long) build. High value for
   power users, but a large scope increase (needs `koji`/`mock`-style
   sandboxed build environment handling, build dependency installation,
   `.config` merge strategy against Fedora's baseline config) — likely a
   v2+ "Advanced" feature behind an explicit opt-in, not part of the
   default kernel list.

Recommendation: ship v1 with the interface stub only (§3.3.4) and revisit
with real user demand data; start with option 1 if a suitable community
COPR is identified, since it requires no new build infrastructure from
this project.

### 11.2 Other distro backends

The `IKernelSource`/`IPackageBackend`/`IBootloaderManager` interfaces are
already distro-agnostic by design. A Debian/Ubuntu backend (effectively
absorbing the original `mainline` project's functionality) would add:
`AptBackend : IPackageBackend` (using `libapt-pkg` or `python3-apt`-style
D-Bus via `aptdaemon`, though `aptdaemon` is largely unmaintained now —
more likely PackageKit's APT backend, keeping `PackageKitBackend` mostly
reusable across distros), and an `UbuntuMainlineSource` porting the
original scraping logic. Not planned for Bumpcap v1/v2 but the
architecture should not need to change to support it later — this is the
reason for keeping backend interfaces decoupled from Fedora-specific
assumptions wherever it doesn't cost extra complexity now.

### 11.3 Background daemon / systemd user service

v1 relies on the GUI's tray icon staying resident for background
monitoring, which only works while a user session with the app running
exists. A future `bumpcap-daemon` (systemd `--user` service,
`bumpcap-check.timer` + `.service` units) could run periodic checks
headlessly and fire notifications even when the GUI isn't open,
reusing `libbumpcap` and `StateStore` directly (same SQLite file,
single-writer discipline already required — see §2.4). Packaging would
add a `%{name}-daemon` subpackage with the timer/service units and an
opt-in enablement step (never auto-enabled by RPM `%post`, per Fedora
packaging guidelines around not auto-starting background services
without explicit user action).

### 11.4 Other possible extensions

- **DKMS awareness:** warn before removing a kernel if DKMS modules are
  currently built against it and no other installed kernel has them
  built, to avoid leaving the user without critical out-of-tree modules
  (e.g. proprietary GPU drivers) on next boot.
- **Secure Boot / kernel signing status indicator:** surface whether a
  candidate kernel (especially CachyOS/mainline) is signed for Secure
  Boot on this system, since unsigned kernels won't boot with Secure
  Boot enabled — important enough to flag prominently given how common a
  support question this is for out-of-tree kernel builds.
- **Multi-machine sync of notes/pins** via an optional export/import
  (already stubbed via CLI `notes-export`/`notes-import` in §6.7) to a
  user-chosen file/cloud-synced folder — deliberately not a built-in
  cloud service, to keep the project infrastructure-free.
- **Flatpak packaging** as an alternative distribution channel for the
  GUI-only portion, though the privileged helper/Polkit/PackageKit
  system-bus integration required for this app's core purpose makes a
  fully sandboxed Flatpak a poor fit without a portal specifically for
  system package management — worth revisiting only if such a portal
  becomes standard.

---

## Appendix A: Glossary

- **BLS (Boot Loader Specification):** freedesktop.org spec for
  per-kernel boot entry snippets, used by Fedora's GRUB2 setup instead of
  a single generated `grub.cfg` kernel list.
- **COPR:** Fedora's community package repository build service —
  user/group-maintained repos, not officially reviewed by Fedora.
- **PackageKit:** cross-desktop, cross-distro D-Bus abstraction over
  native package managers (DNF on Fedora), used by GNOME Software/KDE
  Discover, providing async transactions and Polkit-gated privilege
  escalation.
- **Polkit:** system service for granting privileges to unprivileged
  processes for defined actions, based on policy + optional
  authentication prompt.
- **x86-64-v2/v3/v4:** microarchitecture baseline levels defined by the
  x86-64 psABI, used by CachyOS to ship builds optimized for newer CPUs
  (v3 requires AVX2/BMI2/FMA, roughly Haswell-era 2013+ CPUs and newer).
