Name:           miryu-toolkit
Version:        0.1.0
Release:        1%{?dist}
Summary:        Miryu system utility toolkit

License:        GPL-3.0-or-later
URL:            https://miryu.local/
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  gettext
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kwidgetsaddons-devel

Requires:       (dnf5 or dnf)
Requires:       dnf
Requires:       polkit
Requires:       rpm
Requires:       tar
Requires:       (zstd or gzip)

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
* Thu Aug 14 2026 Miryu <packager@miryu.local> - 0.1.0-4
- Add kernel cleanup button (dnf-3 remove --oldinstallonly) with polkit.

* Thu Aug 14 2026 Miryu <packager@miryu.local> - 0.1.0-3
- Fix collect-logs compilation, add segfault and LiveCD detection, add About tab.

* Wed Aug 13 2026 Miryu <packager@miryu.local> - 0.1.0-2
- Rename to Miryu Toolkit, add system log collection feature.

* Wed Aug 12 2026 Miryu <packager@miryu.local> - 0.1.0-1
- Initial package with Qt6/KF6 UI, polkit helpers and environment variable editor.
