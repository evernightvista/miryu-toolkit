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

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    WidgetsAddons
)

add_executable(miryu-toolkit
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
)

target_compile_definitions(miryu-toolkit PRIVATE
    TOOLKIT_LIBEXEC_DIR="${CMAKE_INSTALL_FULL_LIBEXECDIR}/miryu-toolkit"
)

target_link_libraries(miryu-toolkit
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
)

configure_file(
    polkit/org.miryu.toolkit.policy.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.miryu.toolkit.policy
    @ONLY
)

install(TARGETS miryu-toolkit ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Collect-logs helper (C++ binary, runs as root via pkexec)
add_executable(miryu-toolkit-collect-logs
    src/collect_logs.cpp
)
target_link_libraries(miryu-toolkit-collect-logs
    Qt6::Core
)
install(TARGETS miryu-toolkit-collect-logs
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/miryu-toolkit)

install(PROGRAMS
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
        helpers/miryu-toolkit-cleanup-kernel
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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.miryu.toolkit.policy
        DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)
ki18n_install(po)
