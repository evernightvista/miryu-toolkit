Name:           miryu-toolkit
Version:        45.0.0
Release:        1%{?dist}
Summary:        Miryu Toolkit

License:        GPL-3.0-or-later
URL:            https://github.com/evernightvista/miryu-toolkit
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  gettext
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kwidgetsaddons-devel

Requires:       dnf5
Requires:       /usr/bin/dnf-3
Requires:       polkit
Requires:       rpm
Requires:       tar
Requires:       zstd

%description
Miryu Toolkit is a Qt6 and KDE Frameworks 6 application for managing
optional Miryu components and system-wide environment variables.

It provides polkit-protected helpers for Wine, Steam, MIDI playback support and
additional fonts, plus a system log collection tool.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install
%find_lang %{name}

%files -f %{name}.lang
%license LICENSE
%doc README.md
%{_bindir}/miryu-toolkit
%{_datadir}/applications/miryu-toolkit.desktop
%{_datadir}/applications/terminal-as-root.desktop
%{_datadir}/applications/file-manager-as-root.desktop
%{_datadir}/kio/servicemenus/open-root.desktop
%{_datadir}/metainfo/org.miryu.toolkit.metainfo.xml
%{_datadir}/polkit-1/actions/org.miryu.toolkit.policy
%dir %{_libexecdir}/miryu-toolkit
%{_libexecdir}/miryu-toolkit/miryu-toolkit-wine
%{_libexecdir}/miryu-toolkit/miryu-toolkit-steam
%{_libexecdir}/miryu-toolkit/miryu-toolkit-midi
%{_libexecdir}/miryu-toolkit/miryu-toolkit-extra-fonts
%{_libexecdir}/miryu-toolkit/miryu-toolkit-remove-wine
%{_libexecdir}/miryu-toolkit/miryu-toolkit-remove-steam
%{_libexecdir}/miryu-toolkit/miryu-toolkit-remove-midi
%{_libexecdir}/miryu-toolkit/miryu-toolkit-remove-extra-fonts
%{_libexecdir}/miryu-toolkit/miryu-toolkit-apply-environment
%{_libexecdir}/miryu-toolkit/miryu-toolkit-restore-environment
%{_libexecdir}/miryu-toolkit/miryu-toolkit-cleanup-kernel
%{_libexecdir}/miryu-toolkit/miryu-toolkit-collect-logs

%changelog
* Thu Aug 13 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-1
- Initial Alpha
