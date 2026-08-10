# Notes

## Gotchas
- Fedora uses BLS (Boot Loader Specification) on top of GRUB2. Don't hand-edit grub.cfg — use grubby.
- CachyOS COPR package names may differ from spec assumptions — verify against real repodata before hardcoding.
- Qt SQL connections aren't thread-safe. StateStore needs single-writer discipline (serial task queue).
- libnotify not actually needed — use org.freedesktop.Notifications D-Bus directly.
- PackageKit has no native COPR concept — that's why the helper exists.
- grubby --set-default requires GRUB_DEFAULT=saved (Fedora default, but verify at runtime).

## Patterns
- All privileged ops go through bumpcap-helper + Polkit. GUI/CLI never runs as root.
- IKernelSource implementations use strategy pattern — KernelRepository treats them uniformly.
- KernelInfo is a plain value type (Q_DECLARE_METATYPE), no Qt parent ownership.
- ConfigManager wraps JSON so callers never touch raw JSON. StateStore wraps SQLite similarly.

## Tips
- CMake: set CMAKE_AUTOMOC ON for Qt moc processing
- Build: cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
- Test: ctest --test-dir build --output-on-failure
- Fedora deps: cmake gcc-c++ qt6-qtbase-devel qt6-qttools-devel packagekit-qt6-devel polkit-qt6-1-devel sqlite-devel gtest-devel
