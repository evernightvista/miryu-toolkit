Name:           miryu-toolkit
Version:        45.0.0
Release:        12%{?dist}
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
BuildRequires:  kf6-kcmutils-devel

Requires:       dnf5
Requires:       /usr/bin/dnf-3
Requires:       polkit
Requires:       rpm
Requires:       tar
Requires:       zstd
Requires:       inxi
Requires:       lshw
Requires:       grub2-tools
Requires:       grubby
Obsoletes:      evernight-vista-tools
Obsoletes:      miryu-grub-config < %{version}-%{release}

%description
Miryu Toolkit is a Qt6 and KDE Frameworks 6 application for managing
optional Miryu components and system-wide environment variables.

It provides polkit-protected helpers for Wine, Steam, MIDI playback support and
additional fonts, plus a system log collection tool. It also integrates a GRUB2
boot menu configuration tool with kernel parameter management.

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
%{_datadir}/polkit-1/actions/org.miryugaming.toolkit.policy
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
%{_libexecdir}/miryu-toolkit/miryu-toolkit-update-system
%{_libexecdir}/miryu-toolkit/miryu-toolkit-unlock-rpm
%{_libexecdir}/miryu-toolkit/miryu-toolkit-autoremove
%{_libexecdir}/miryu-toolkit/miryu-toolkit-collect-logs
%{_libexecdir}/miryu-toolkit/miryu-toolkit-view-crash
%{_libexecdir}/miryu-toolkit/miryu-toolkit-grub-config-helper
%{_qt6_plugindir}/plasma/kcms/systemsettings_qwidgets/kcm_miryu_toolkit.so


%changelog
* Tue Sep 22 2026 KairikiFedora <13278297951@sina.cn> - 45.0.0-12
- Fix KCM not translate

* Tue Sep 22 2026 KairikiFedora <13278297951@sina.cn> - 45.0.0-11
- Merge miryu-grub-config GUI into Miryu System Assistant tab as a new
  "Boot Menu" section with GRUB2 boot menu and kernel parameter controls
- Remove standalone GRUB config title/subtitle and reboot button from
  the merged UI
- Add miryu-toolkit-grub-config-helper polkit helper for saving GRUB2
  settings and regenerating bootloader configuration
- Rename all polkit action IDs from org.miryu.toolkit.* to
  org.miryugaming.toolkit.*
- Add org.miryugaming.toolkit.save-grub polkit action
- Merge 57 translation strings from miryu-grub-config into all 6 .po files
- Add Requires for grub2-tools and grubby
- Obsoletes miryu-grub-config

* Sat Sep 05 2026 KairikiFedora <13278297951@sina.cn> - 45.0.0-10
- Bump to 45.0.0-10

* Sat Sep 05 2026 KairikiFedora <13278297951@sina.cn> - 45.0.0-9
- Remove Personalization Module

* Tue Aug 25 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-8
- Integrated with KDE System Settings
- Fix collect dnf5 history error

* Sun Aug 23 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-7
- Update Icon

* Thu Aug 20 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-6
- Fix __NO_SEGFAULT_FOUND__ marker replacement in correct viewCrashInfo lambda
- Fix blur apply timing: add delays after kwriteconfig6 and between
  unload/load to ensure KWin reads fresh config values
- Remove 12 unused translation entries from all 6 .po files
- Change qdbus6 to qdbus-qt6 for blur effect unload/load
- Fix "No segfault entries found" not localized: helper outputs marker
  __NO_SEGFAULT_FOUND__, GUI replaces with i18n() string
- Fix unload/load blur effect: run as two separate sequential QProcess
  calls instead of sh -c with && operator
- Add personalization translations for de, fr, ja, ko
- Rename levels: Transparent→Real, Frosted→Soft
- Update descriptions for Default and Soft levels
- Fix blur not applying: unload+load effect instead of reconfigureEffect
- Update zh_CN and zh_TW translations
- Fix blur effect not applying: use kwriteconfig6 (KConfig) instead of
  direct QFile writes so KWin's config cache is properly notified
- Use reconfigureEffect D-Bus call to reload blur effect in real-time
- Add Requires for kwriteconfig6 and qdbus6
- Add Personalization tab with Interface Transparency section
- Three transparency levels: Transparent, Default, Frosted
- Click-to-select image cards with hover descriptions
- Apply changes to kwinrc [Effect-blur] and reload KWin in real-time

* Mon Aug 17 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-5
- Add desktop search keywords (Miryu, Toolkit, gongjuxiang, miryu)
- Change desktop Categories from System to Utility
- Make view-crash polkit action require no authentication (allow=yes)

* Mon Aug 17 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-4
- Fix crash info viewer to run dmesg as root via pkexec so segfault
  entries are visible, matching "sudo dmesg | grep segfault"
- Add miryu-toolkit-view-crash polkit helper

* Mon Aug 17 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-3
- Add Miryu System Assistant tab with systemd, RPM, dnf5 and crash info tools
- Rename Miscellaneous tab to Install Additional Components
- Add unlock RPM database and dnf5 autoremove helpers

* Mon Aug 17 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-2
- Add Update System Button

* Thu Aug 13 2026 Evernight Vista Team <13278297951@sina.cn> - 45.0.0-1
- Initial Alpha
