Name:           bumpcap
Version:        0.1.0
Release:        1%{?dist}
Summary:        Browse, install, and manage Linux kernels on Fedora
License:        GPL-3.0-or-later
URL:            https://github.com/bumpcap/bumpcap
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
pinning, and managing Linux kernels on Fedora from Fedora's own repositories,
Rawhide, and CachyOS-patched builds distributed via COPR.

%package cli
Summary:        Command-line interface for Bumpcap
Requires:       %{name} = %{version}-%{release}

%description cli
Headless and scriptable CLI for Bumpcap's kernel management features.

%package helper
Summary:        Privileged D-Bus helper for Bumpcap
Requires:       dnf5
Requires:       dnf5-plugins
Requires:       grub2-tools
Requires:       grubby
Requires:       polkit

%description helper
Polkit-mediated helper performing privileged operations such as COPR repository
management and GRUB default kernel changes on behalf of Bumpcap.

%prep
%autosetup

%build
%cmake \
    -DKERNELHUB_BUILD_GUI=ON \
    -DKERNELHUB_BUILD_CLI=ON \
    -DKERNELHUB_BUILD_HELPER=ON \
    -DKERNELHUB_BUILD_TESTS=OFF
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
%{_datadir}/icons/hicolor/*/apps/org.bumpcap.Bumpcap.svg

%files cli
%{_bindir}/bumpcap-cli

%files helper
%{_libexecdir}/bumpcap/bumpcap-helper
%{_datadir}/dbus-1/system-services/org.bumpcap.Helper1.service
%config(noreplace) %{_sysconfdir}/dbus-1/system.d/org.bumpcap.Helper1.conf
%{_datadir}/polkit-1/actions/org.bumpcap.policy

%changelog
* Mon Aug 09 2026 Bumpcap Developers <bumpcap@example.com> - 0.1.0-1
- Initial package
