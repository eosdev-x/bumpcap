# Security Review — Bumpcap (Jeff 🛡️)

**Reviewer:** Jeff (Security Gate)
**Date:** 2026-08-09
**Scope:** Full codebase security audit — privileged helper, D-Bus policy, Polkit, package management, bootloader, network, state/config, input surfaces
**Severity Scale:** 🔴 BLOCKER | 🟡 WARNING | 🔵 SUGGESTION | ✅ PASS

---

## 1. Privileged Helper (helper/HelperService.cpp)

### ✅ PASS — Command Injection via QProcess

All `QProcess` calls use argv-style argument lists, never shell strings. `runCommand()` takes a `QStringList` of arguments and calls `process.setArguments()`. No shell is involved. This is the correct pattern.

**Evidence:** `HelperService.cpp:153-156` — `process.setProgram(program); process.setArguments(arguments);`

### ✅ PASS — Polkit Enforcement

Every privileged operation checks Polkit authorization *before* executing:
- `EnableCopr` → `authorize(kManageCoprAction)` at line 30
- `DisableCopr` → `authorize(kManageCoprAction)` at line 43
- `SetDefaultKernel` → `authorize(kSetDefaultKernelAction)` at line 56
- `RegenerateGrubConfig` → `authorize(kRegenerateGrubAction)` at line 66

The `authorize()` method resolves the caller PID from D-Bus and checks via `PolkitQt1::Authority::checkAuthorizationSync()`. Cannot be bypassed without Polkit root exploit.

### ✅ PASS — COPR Argument Allow-Listing

`validateCopr()` (lines 78-97) enforces:
1. Regex `^[A-Za-z0-9_-]+$` on both owner and project
2. Owner must equal `bieszczaders`
3. Project must start with `kernel-cachyos`

This prevents enabling arbitrary COPR repos via the helper.

### ✅ PASS — Kernel Path Validation

`validateKernelPath()` (lines 99-113) enforces:
1. Must start with `/boot/vmlinuz-`
2. No `..`, `\0`, or `//`
3. Regex `^/boot/vmlinuz-[A-Za-z0-9._:+-]+$` on the full path

This prevents path traversal and injection of special characters.

### ✅ PASS — Timeout Protection

`runCommand()` enforces a 10-minute timeout (`kCommandTimeoutMs`). If exceeded, the process is killed. Prevents resource exhaustion from hung commands.

---

## 2. Polkit Policy (helper/org.bumpcap.policy)

### ✅ PASS — Default Authorizations

All three actions use `auth_admin` for `allow_any`, `allow_inactive`, and `allow_active`. This requires administrator authentication for every privileged operation — no silent privilege escalation.

### ✅ PASS — Vendor Metadata

`<vendor>Bumpcap</vendor>` and `<vendor_url>` are present.

### 🔵 SUGGESTION — Action Descriptions Could Be More Specific

**File:** `helper/org.bumpcap.policy`
**Concern:** The description "Manage COPR repositories for Bumpcap" doesn't tell the user *which* repo will be enabled. A user seeing the Polkit dialog has no way to know they're enabling `bieszczaders/kernel-cachyos`.

**Recommended fix:** Change to:
```xml
<description>Enable or disable the CachyOS kernel COPR repository (bieszczaders/kernel-cachyos)</description>
<message>Authentication is required to enable or disable the CachyOS kernel COPR repository for kernel installation</message>
```

---

## 3. D-Bus Configuration (helper/org.bumpcap.Helper1.conf)

### ✅ PASS — Service Ownership

Only `root` can own `org.bumpcap.Helper1` (line 5: `<allow own="org.bumpcap.Helper1"/>` under `<policy user="root">`).

### 🟡 WARNING — Signals Exposed to All Users

**File:** `helper/org.bumpcap.Helper1.conf`, lines 17-20
**Vulnerability:** The `Progress` and `Completed` signals are in the default policy, allowing any local user to eavesdrop on privileged operations.

```xml
<allow send_destination="org.bumpcap.Helper1"
       send_interface="org.bumpcap.Helper1"
       send_member="Progress"/>
<allow send_destination="org.bumpcap.Helper1"
       send_interface="org.bumpcap.Helper1"
       send_member="Completed"/>
```

**Attack scenario:** An unprivileged user monitors D-Bus signal traffic to learn:
- When an administrator is performing privileged operations (timing side-channel)
- Whether operations succeeded or failed (information disclosure)
- The names of operations being performed

**Recommended fix:** Restrict signal reception to users who initiated the operation, or remove the blanket allow and rely on D-Bus eavesdropping rules being disabled by default (which they are on most systems). Alternatively, the helper could track the caller UID and only send signals to that specific connection. A minimal fix: remove the two signal `<allow>` entries — signals on the system bus are not delivered to arbitrary listeners unless explicitly allowed.

---

## 4. Package Management

### ✅ PASS — PackageKitBackend

- Uses PackageKit's D-Bus API with `kTransactionFlagOnlyTrusted = 1u << 0` for both install and remove operations. This ensures only signed packages from trusted repos are installed.
- Version strings come from PackageKit's own `Resolve` API, not from user input.
- Polkit is enforced by PackageKit itself.

### ✅ PASS — DnfCliBackend Command Safety

`QProcess` uses argv-style argument lists. Package specs are constructed as `packageName + '-' + version` — no shell metacharacters can escape. Example: `kernel-core-6.10.9-200.fc40.x86_64` is passed as a single argument to dnf.

### ✅ PASS — Remove Guardrails

Both `PackageKitBackend::removalBlocked()` and `DnfCliBackend::removalBlocked()` enforce:
1. Cannot remove the currently running kernel (checked via `IsKernelRunning()`)
2. Cannot remove a pinned kernel without `force=true`

These checks are in `libbumpcap`, so both GUI and CLI benefit.

### 🔵 SUGGESTION — pkexec Fallback Without Root Check

**File:** `core/pkg/DnfCliBackend.cpp`, lines 131-140
**Concern:** If `pkexec` is not found, the backend falls back to running `dnf` directly (no privilege escalation). If the GUI is somehow running as root (violating the spec), this would execute without Polkit authentication.

**Attack scenario:** User runs `sudo bumpcap` (against spec recommendations). DnfCliBackend runs dnf without Polkit prompts.

**Recommended fix:** Log a warning when pkexec is unavailable. Consider refusing to operate without pkexec unless an explicit `--allow-root` flag is passed.

---

## 5. Bootloader (core/boot/Grub2BlsManager.cpp)

### 🔴 BLOCKER — setDefaultEntry() Bypasses Helper Path Validation

**File:** `core/boot/Grub2BlsManager.cpp`, lines 36-48
**Vulnerability:** `setDefaultEntry()` calls `pkexec grubby --set-default <entryId>` directly, bypassing the D-Bus helper (`bumpcap-helper`) and its `validateKernelPath()` check.

```cpp
void Grub2BlsManager::setDefaultEntry(const QString &entryId) {
    QStringList arguments{QStringLiteral("--set-default"), entryId};
    const QString program = privilegedProgram(&arguments, QStringLiteral("grubby"));
    // ^^^ calls pkexec grubby directly — no validation of entryId
    operation_process_->setProgram(program);
    operation_process_->setArguments(arguments);
    operation_process_->start();
}
```

**Attack scenario:** The `entryId` is parsed from `grubby --info=ALL` output. If a malicious BLS entry file exists in `/boot/loader/entries/` (e.g., placed by an attacker with write access to `/boot`), the entry's `id=` or `kernel=` field could contain crafted content. While grubby itself would process the argument, the lack of validation means Bumpcap passes untrusted data directly to a privileged operation.

More critically, this bypasses the architectural pattern established by the helper — all privileged bootloader operations should go through the Polkit-gated D-Bus helper. The helper has `SetDefaultKernel` with `validateKernelPath()`. This code path skips it entirely.

**Recommended fix:** Route `setDefaultEntry()` through the D-Bus helper's `SetDefaultKernel` method instead of calling pkexec directly:

```cpp
void Grub2BlsManager::setDefaultEntry(const QString &entryId) {
    QDBusInterface iface(QStringLiteral("org.bumpcap.Helper1"),
                         QStringLiteral("/org/bumpcap/Helper1"),
                         QStringLiteral("org.bumpcap.Helper1"),
                         QDBusConnection::systemBus());
    iface.asyncCall(QStringLiteral("SetDefaultKernel"), entryId);
}
```

Or add local validation mirroring `validateKernelPath()` before the pkexec call.

### 🟡 WARNING — regenerateConfig() and rebootIntoEntryOnce() Also Bypass Helper

**File:** `core/boot/Grub2BlsManager.cpp`, lines 50-78
**Concern:** `regenerateConfig()` and `rebootIntoEntryOnce()` also call pkexec directly instead of using the D-Bus helper. The helper has `RegenerateGrubConfig` (with Polkit action `org.bumpcap.regenerate-grub`), but these methods don't use it.

For `regenerateConfig()`, the arguments are hardcoded (`-o /boot/grub2/grub.cfg`), so injection risk is low. For `rebootIntoEntryOnce()`, the entryId comes from grubby output — same concern as above.

**Recommended fix:** Route both through the D-Bus helper for consistency and centralized Polkit enforcement.

### ✅ PASS — grub2-mkconfig Output Path

The output path `/boot/grub2/grub.cfg` is hardcoded in both the helper and Grub2BlsManager. Not user-controllable.

---

## 6. Network (core/net/NetworkClient.cpp)

### ✅ PASS — HTTPS Enforcement

`QNetworkRequest::NoLessSafeRedirectPolicy` (line 25) ensures HTTP→HTTPS redirects are allowed but HTTPS→HTTP redirects are blocked. All hardcoded URLs use HTTPS:
- `https://copr.fedorainfracloud.org/api_3/...`
- `https://download.copr.fedorainfracloud.org/results/...`
- `https://dl.fedoraproject.org/pub/fedora/linux/...`

### ✅ PASS — SSL Verification

Qt's `QNetworkAccessManager` performs SSL certificate verification by default. No code disables it.

### ✅ PASS — Redirect Policy

`NoLessSafeRedirectPolicy` prevents protocol downgrade attacks via redirects.

### 🔵 SUGGESTION — No Certificate Pinning

**File:** `core/net/NetworkClient.cpp`
**Concern:** While SSL verification is enabled, there's no certificate pinning for the three known-good domains (COPR, Fedora mirrors). A compromised CA could issue fraudulent certificates.

**Risk level:** Low. Certificate pinning is complex to maintain and has its own failure modes. This is a defense-in-depth suggestion for a future hardening pass.

---

## 7. State/Config

### ✅ PASS — SQL Injection Prevention

All SQLite queries in `StateStore.cpp` use parameterized queries via `query.prepare()` + `query.addBindValue()`. No string concatenation of user input into SQL. Verified across all methods: `setPinned`, `removePin`, `isPinned`, `setNote`, `note`, `markSeen`, `setNotified`, `recordInstallAction`, `installHistory`, `schemaVersion`, `setSchemaVersion`.

### 🔵 SUGGESTION — Database File Permissions

**File:** `core/state/StateStore.cpp`, `ensureDatabaseDirectory()`
**Concern:** The database directory and file are created with default umask permissions. On a typical system, this means `0644`/`0755` — world-readable. While the database is user-specific (under `~/.local/share/`), explicit `0600` would be more defensive.

**Recommended fix:** After creating the database, set permissions:
```cpp
QFile::setPermissions(database_path_, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
```

### ✅ PASS — Migration Safety

Migrations are loaded from Qt embedded resources (`:/kh/state/migrations/`), not from the filesystem. Cannot be tampered with by an attacker who only has filesystem write access to the user's home directory.

### ✅ PASS — Config Path Hardcoded

`ConfigManager::DefaultConfigPath()` returns `~/.config/bumpcap/config.json` via `QStandardPaths::ConfigLocation`. Not user-controllable.

### ✅ PASS — Atomic Config Writes

`ConfigManager::save()` uses `QSaveFile`, which writes to a temporary file and atomically renames. Prevents config corruption from interrupted writes.

---

## 8. Input Surfaces

### ✅ PASS — GUI XSS Prevention

- `DetailPanel` uses `setPlainText()` for changelog, dependencies, and notes — not `setHtml()`. Rich text is disabled for notes (`setAcceptRichText(false)`). No XSS vector.
- `KernelTableModel` uses plain text display in table cells.

### ✅ PASS — CLI Input Validation

- `boot-order --set-default=<version>` prepends `/boot/vmlinuz-` if the argument doesn't start with it, then passes through the D-Bus helper which validates with regex.
- `note <version> "text"` — version is a database key, no injection vector.
- `pin/unpin <version>` — same.

### ✅ PASS — Network Data Treated as Untrusted

- COPR API JSON is parsed but only used for display (not trusted for install decisions).
- Repodata XML is parsed with `QXmlStreamReader` (not external entity expansion).
- Package names from repodata are filtered against `PackageNamesForSource()` allow-list before use.
- Version strings are validated by grouping logic before being used in package specs.

---

## 9. Additional Findings

### 🟡 WARNING — CachyOsCoprSource Bypasses Helper for Repo Enable

**File:** `core/sources/CachyOsCoprSource.cpp`, lines 62-83
**Vulnerability:** `ensureRepoEnabled()` calls `pkexec dnf5 copr enable bieszczaders/kernel-cachyos -y` directly, bypassing the D-Bus helper's `validateCopr()` checks and Polkit action `org.bumpcap.manage-copr`.

```cpp
void CachyOsCoprSource::ensureRepoEnabled() {
    // ...
    repo_enable_process_->setProgram(pkexec);
    repo_enable_process_->setArguments({dnf,
                                        QStringLiteral("copr"),
                                        QStringLiteral("enable"),
                                        QStringLiteral("bieszczaders/kernel-cachyos"),
                                        QStringLiteral("-y")});
}
```

While the COPR name is hardcoded (not user-controlled), this bypasses the architectural pattern where all privileged COPR operations go through the Polkit-gated helper with `org.bumpcap.manage-copr` action. This means:
1. The Polkit prompt will show a generic pkexec dialog instead of Bumpcap's custom action description
2. No audit trail in the helper's logging
3. Inconsistency with the spec's design intent

**Recommended fix:** Use the D-Bus helper's `EnableCopr` method:
```cpp
QDBusInterface iface(QStringLiteral("org.bumpcap.Helper1"),
                     QStringLiteral("/org/bumpcap/Helper1"),
                     QStringLiteral("org.bumpcap.Helper1"),
                     QDBusConnection::systemBus());
iface.asyncCall(QStringLiteral("EnableCopr"),
                QStringLiteral("kernel-cachyos"),
                QStringLiteral("bieszczaders"));
```

### 🔵 SUGGESTION — IsKernelRunning() Uses Substring Matching

**File:** `core/sources/KernelPackageUtils.cpp`, lines 133-136

```cpp
bool IsKernelRunning(const kh::model::KernelInfo &kernel) {
    const QString running = RunningKernelVersion();
    return !running.isEmpty() && (kernel.version == running || kernel.version.contains(running) ||
                                  running.contains(kernel.shortVersion));
}
```

**Concern:** The `contains()` checks could cause false positives. E.g., if running kernel is `6.10.9` and a kernel `6.10.90` exists, `running.contains("6.10.9")` would match.

**Recommended fix:** Use exact version matching only, or compare after normalizing to the full `version-release.arch` format.

### 🔵 SUGGESTION — Timeout Constant Could Be Tighter

**File:** `helper/HelperService.cpp`, line 14
**Current:** `constexpr int kCommandTimeoutMs = 10 * 60 * 1000;` (10 minutes)
**Concern:** `grubby --set-default` and `grub2-mkconfig` should complete in seconds. 10 minutes is generous. Consider 2 minutes for most operations, or per-operation timeouts.

---

## Security Verdict

# 🔴 BLOCKED — 1 Blocker Must Be Fixed

The codebase demonstrates strong security fundamentals: parameterized SQL, QProcess argv lists, Polkit enforcement, COPR allow-listing, HTTPS enforcement, and plain-text UI rendering. The privileged helper is well-designed with proper input validation.

However, **Grub2BlsManager::setDefaultEntry() bypasses the helper's path validation**, violating the security architecture's core principle that all privileged operations go through the Polkit-gated D-Bus helper.

---

## Attack Surface Summary

| Trust Boundary | What Crosses It | Protection Mechanism |
|---|---|---|
| Unprivileged GUI/CLI → Root helper | COPR enable/disable, kernel default, GRUB regen | D-Bus + Polkit (auth_admin) |
| Unprivileged GUI/CLI → PackageKit | Package install/remove | PackageKit D-Bus + Polkit |
| Unprivileged GUI/CLI → pkexec grubby | **setDefaultEntry, rebootInto, regenConfig** | **pkexec only (no helper validation)** ⚠️ |
| Unprivileged GUI/CLI → pkexec dnf | **COPR enable** ⚠️ | **pkexec only (no helper validation)** ⚠️ |
| Network → Application | COPR API JSON, repodata XML | HTTPS, XML parsing, name allow-list |
| User input → SQLite | Notes, pins | Parameterized queries |
| User input → GUI display | Version, changelog, notes | Plain text rendering |
| BLS entries → grubby args | Entry IDs from `/boot/loader/entries/` | **No validation in Grub2BlsManager** ⚠️ |

---

## Priority Fixes (Ordered)

1. **🔴 [BLOCKER] Route `Grub2BlsManager::setDefaultEntry()` through D-Bus helper** — Add validation before the pkexec call, or use the helper's `SetDefaultKernel` D-Bus method. Prevents unvalidated input reaching grubby.

2. **🟡 Route `regenerateConfig()` and `rebootIntoEntryOnce()` through D-Bus helper** — Same pattern as above. Lower risk since arguments are more constrained, but violates the architectural principle.

3. **🟡 Route `CachyOsCoprSource::ensureRepoEnabled()` through D-Bus helper** — Use `EnableCopr` D-Bus call instead of direct pkexec. Gets proper Polkit action description and audit logging.

4. **🟡 Remove `Progress`/`Completed` signal allow from D-Bus policy** — Prevents information leakage about privileged operations to unprivileged users.

5. **🔵 Improve Polkit action descriptions** — Specify which COPR repo is being managed.

6. **🔵 Set explicit file permissions on state.db** — `0600` instead of umask default.

7. **🔵 Fix `IsKernelRunning()` substring matching** — Use exact match to prevent false positives.

8. **🔵 Add pkexec fallback warning in DnfCliBackend** — Log when running without Polkit protection.
