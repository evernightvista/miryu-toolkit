cmake_minimum_required(VERSION 3.16)

project(miryu-toolkit VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(GNUInstallDirs)
include(KDEInstallDirs)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDECMakeSettings)
include(CTest)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    KCMUtils
    WidgetsAddons
)

add_library(miryu-boot-core STATIC
    src/bootbackup.cpp
    src/bootbackup.h
    src/bootdata.cpp
    src/bootdata.h
)
set_target_properties(miryu-boot-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(miryu-boot-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(miryu-boot-core PUBLIC
    MOK_LIST_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit/miryu-toolkit-list-mok"
)
target_link_libraries(miryu-boot-core PUBLIC Qt6::Core KF6::I18n)

add_executable(miryu-toolkit
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/grubconfig.cpp
    src/grubconfig.h
    src/bootrepairpage.cpp
    src/bootrepairpage.h
    src/bootservice.cpp
    src/bootservice.h
    src/uefibootpage.cpp
    src/uefibootpage.h
)

target_compile_definitions(miryu-toolkit PRIVATE
    TOOLKIT_LIBEXEC_DIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit"
    GRUB_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit/miryu-toolkit-grub-config-helper"
    BOOT_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit/miryu-toolkit-boot-helper"
    GRUB_DEFAULT_PATH="/etc/default/grub"
    BLS_ENTRIES_PATH="/boot/loader/entries"
)

target_link_libraries(miryu-toolkit
    miryu-boot-core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
)

configure_file(
    polkit/org.miryugaming.toolkit.policy.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.miryugaming.toolkit.policy
    @ONLY
)

install(TARGETS miryu-toolkit ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# KCM (KDE System Settings module) that launches the external Miryu Toolkit GUI.
# Widgets-based KCMs install into the plasma/kcms/systemsettings_qwidgets plugin
# namespace so System Settings discovers them; the JSON metadata registers the
# module under the "System" section (X-KDE-System-Settings-Parent-Category).
kcoreaddons_add_plugin(kcm_miryu_toolkit
    SOURCES kcm/kcm_miryu_toolkit.cpp
    INSTALL_NAMESPACE "plasma/kcms/systemsettings_qwidgets"
)
target_link_libraries(kcm_miryu_toolkit
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtils
)

# Collect-logs helper (C++ binary, runs as root via pkexec)
add_executable(miryu-toolkit-collect-logs
    src/collect_logs.cpp
)
target_link_libraries(miryu-toolkit-collect-logs
    Qt6::Core
    KF6::I18n
)
install(TARGETS miryu-toolkit-collect-logs
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)

# GRUB config helper (C++ binary, runs as root via pkexec)
add_executable(miryu-toolkit-grub-config-helper
    src/grubconfighelper.cpp
)
target_link_libraries(miryu-toolkit-grub-config-helper
    miryu-boot-core
    Qt6::Core
    KF6::I18n
)
target_compile_definitions(miryu-toolkit-grub-config-helper PRIVATE
    GRUB_DEFAULT_PATH="/etc/default/grub"
    GRUB_DROPIN_PATH="/etc/default/grub.d/99_miryu_boot.cfg"
    GRUB_CFG_PATH="/boot/grub2/grub.cfg"
)
install(TARGETS miryu-toolkit-grub-config-helper
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)

# UEFI, Secure Boot, GRUB repair and configuration restore helper.
add_executable(miryu-toolkit-boot-helper
    src/boothelper.cpp
)
target_link_libraries(miryu-toolkit-boot-helper
    miryu-boot-core
    Qt6::Core
)
install(TARGETS miryu-toolkit-boot-helper
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)

# Dedicated read-only MOK helper. A separate executable gives the silent
# polkit action an unambiguous exec.path and cannot perform write operations.
add_executable(miryu-toolkit-list-mok
    src/boothelper.cpp
)
target_compile_definitions(miryu-toolkit-list-mok PRIVATE MIRYU_MOK_LIST_HELPER)
target_link_libraries(miryu-toolkit-list-mok
    miryu-boot-core
    Qt6::Core
)
install(TARGETS miryu-toolkit-list-mok
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)

install(PROGRAMS
        helpers/miryu-toolkit-fedora-kernel
        helpers/miryu-toolkit-remove-fedora-kernel
        helpers/miryu-toolkit-wine
        helpers/miryu-toolkit-steam
        helpers/miryu-toolkit-midi
        helpers/miryu-toolkit-extra-fonts
        helpers/miryu-toolkit-remove-wine
        helpers/miryu-toolkit-remove-steam
        helpers/miryu-toolkit-remove-midi
        helpers/miryu-toolkit-remove-extra-fonts
        helpers/miryu-toolkit-apply-environment
        helpers/miryu-toolkit-restore-environment
        helpers/miryu-toolkit-apply-sleep
        helpers/miryu-toolkit-fix-sleep
        helpers/miryu-toolkit-cleanup-kernel
        helpers/miryu-toolkit-update-system
        helpers/miryu-toolkit-unlock-rpm
        helpers/miryu-toolkit-autoremove
        helpers/miryu-toolkit-view-crash
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)
install(FILES org.miryu.toolkit/miryu-toolkit.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES
        data/applications/terminal-as-root.desktop
        data/applications/file-manager-as-root.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/servicemenus/open-root.desktop
        DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES org.miryu.toolkit/org.miryu.toolkit.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.miryugaming.toolkit.policy
        DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

    add_executable(test-boot-core tests/testbootcore.cpp)
    target_link_libraries(test-boot-core miryu-boot-core Qt6::Test)
    add_test(NAME boot-core COMMAND test-boot-core)
    set_tests_properties(boot-core PROPERTIES TIMEOUT 120)

    add_executable(test-boot-helper tests/testboothelper.cpp)
    target_compile_definitions(test-boot-helper PRIVATE MIRYU_BOOT_HELPER_TEST)
    target_link_libraries(test-boot-helper miryu-boot-core Qt6::Test)
    add_test(NAME boot-helper COMMAND test-boot-helper)
    set_tests_properties(boot-helper PROPERTIES TIMEOUT 120)

    add_executable(test-boot-pages
        tests/testbootpages.cpp
        src/mainwindow.cpp
        src/grubconfig.cpp
        src/bootrepairpage.cpp
        src/bootservice.cpp
        src/uefibootpage.cpp
    )
    target_compile_definitions(test-boot-pages PRIVATE
        MIRYU_BOOT_PAGES_TEST
        TOOLKIT_LIBEXEC_DIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit"
        GRUB_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit/miryu-toolkit-grub-config-helper"
        BOOT_HELPER_PATH="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit/miryu-toolkit-boot-helper"
        GRUB_DEFAULT_PATH="/etc/default/grub"
        BLS_ENTRIES_PATH="/boot/loader/entries"
    )
    target_link_libraries(test-boot-pages
        miryu-boot-core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
        Qt6::Test
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
    )
    add_test(NAME boot-pages COMMAND test-boot-pages)
    set_tests_properties(boot-pages PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
endif()

ki18n_install(po)
