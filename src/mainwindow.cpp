#include "mainwindow.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <signal.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <memory>

#ifndef TOOLKIT_LIBEXEC_DIR
#define TOOLKIT_LIBEXEC_DIR "/usr/libexec/miryu-toolkit"
#endif

#ifndef GRUB_HELPER_PATH
#define GRUB_HELPER_PATH "/usr/libexec/miryu-toolkit/miryu-toolkit-grub-config-helper"
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_grubConfig(new GrubConfig(this))
{
    setupComponents();

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(10);

    auto *title = new QLabel(i18n("Miryu Toolkit"), central);
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 8);
    titleFont.setBold(true);
    title->setFont(titleFont);
    rootLayout->addWidget(title);

    auto *subtitle = new QLabel(i18n("Manage Miryu components, maintenance tasks, system-wide environment variables and sleep settings in one place."), central);
    subtitle->setWordWrap(true);
    rootLayout->addWidget(subtitle);

    m_tabs = new QTabWidget(central);

    auto wrapScroll = [](QWidget *content) -> QWidget * {
        auto *scroll = new QScrollArea;
        scroll->setWidget(content);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        return scroll;
    };

    m_tabs->addTab(wrapScroll(buildSystemAssistantTab()), i18n("Miryu System Assistant"));
    m_tabs->addTab(wrapScroll(buildBootMenuTab()), i18n("Boot Menu"));
    m_tabs->addTab(wrapScroll(buildEnvironmentTab()), i18n("System-wide environment variables"));
    m_tabs->addTab(wrapScroll(buildSleepTab()), i18n("Sleep"));
    m_tabs->addTab(wrapScroll(buildExtrasTab()), i18n("Install additional components"));
    m_tabs->addTab(wrapScroll(buildAboutTab()), i18n("About"));
    rootLayout->addWidget(m_tabs, 1);

    auto *bottomLayout = new QHBoxLayout;
    m_bottomStatus = new QLabel(i18n("Ready"), central);
    bottomLayout->addWidget(m_bottomStatus);
    bottomLayout->addStretch();
    rootLayout->addLayout(bottomLayout);

    setCentralWidget(central);
    setMinimumSize(980, 620);
    resize(980, 620);
    setWindowTitle(i18n("Miryu Toolkit"));

    refreshComponentStates();
    loadEnvironmentVariables();
    loadSleepConfig();
    loadGrubConfig();
}

void MainWindow::setupComponents()
{
    m_components = {
        {
            QStringLiteral("fedora-kernel"),
            i18n("Install Fedora official kernel"),
            i18n("If some games' anti-cheat systems use Epic EAC Linux v2 and cannot start, you can install the Fedora official kernel to solve the problem."),
            QStringLiteral("miryu-toolkit-fedora-kernel"),
            QStringLiteral("miryu-toolkit-remove-fedora-kernel"),
            {
                QStringLiteral("kernel"),
                QStringLiteral("kernel-core"),
                QStringLiteral("kernel-modules"),
                QStringLiteral("kernel-modules-core"),
                QStringLiteral("kernel-modules-extra"),
            },
        },
        {
            QStringLiteral("wine"),
            i18n("Install Wine"),
            i18n("Install Wine, DXVK, Winetricks and WineASIO for running Windows programs and some audio workflows."),
            QStringLiteral("miryu-toolkit-wine"),
            QStringLiteral("miryu-toolkit-remove-wine"),
            {
                QStringLiteral("terra-wine-dxvk-d3d9"),
                QStringLiteral("terra-wine-dxvk"),
                QStringLiteral("terra-wine-dxvk-d3d10"),
                QStringLiteral("winetricks-git"),
                QStringLiteral("wineasio"),
            },
        },
        {
            QStringLiteral("steam"),
            i18n("Install Steam"),
            i18n("Install the Steam client. Steam will download update files on first launch."),
            QStringLiteral("miryu-toolkit-steam"),
            QStringLiteral("miryu-toolkit-remove-steam"),
            {QStringLiteral("steam")},
        },
        {
            QStringLiteral("midi"),
            i18n("Install MIDI playback support"),
            i18n("Install the FluidSynth GStreamer plugin to enable MIDI file playback support."),
            QStringLiteral("miryu-toolkit-midi"),
            QStringLiteral("miryu-toolkit-remove-midi"),
            {QStringLiteral("gstreamer1-plugins-bad-free-fluidsynth")},
        },
        {
            QStringLiteral("fonts"),
            i18n("Install additional fonts"),
            i18n("Install Noto Sans, Noto Serif, monospace and CJK supplemental fonts to improve multilingual display."),
            QStringLiteral("miryu-toolkit-extra-fonts"),
            QStringLiteral("miryu-toolkit-remove-extra-fonts"),
            {
                QStringLiteral("google-noto-sans-fonts.noarch"),
                QStringLiteral("google-noto-sans-mono-fonts.noarch"),
                QStringLiteral("google-noto-serif-fonts.noarch"),
                QStringLiteral("google-noto-serif-cjk-vf-fonts"),
                QStringLiteral("google-noto-sans-mono-cjk-vf-fonts"),
            },
        },
    };
}

QWidget *MainWindow::buildSystemAssistantTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *group = new QGroupBox(i18n("Miryu System Assistant"), page);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(10);

    auto *cleanupKernelButton = new QPushButton(i18n("Clean up previous kernel"), group);
    cleanupKernelButton->setMinimumWidth(320);
    connect(cleanupKernelButton, &QPushButton::clicked, this, &MainWindow::cleanupOldKernel);
    groupLayout->addWidget(cleanupKernelButton);

    auto *updateSystemButton = new QPushButton(i18n("Update system"), group);
    updateSystemButton->setMinimumWidth(320);
    connect(updateSystemButton, &QPushButton::clicked, this, &MainWindow::updateSystem);
    groupLayout->addWidget(updateSystemButton);

    auto *listFailedButton = new QPushButton(i18n("List failed systemd service units"), group);
    listFailedButton->setMinimumWidth(320);
    connect(listFailedButton, &QPushButton::clicked, this, &MainWindow::listFailedServices);
    groupLayout->addWidget(listFailedButton);

    auto *unlockRpmButton = new QPushButton(i18n("Unlock RPM database"), group);
    unlockRpmButton->setMinimumWidth(320);
    connect(unlockRpmButton, &QPushButton::clicked, this, &MainWindow::unlockRpmDatabase);
    groupLayout->addWidget(unlockRpmButton);

    auto *viewDnf5LogButton = new QPushButton(i18n("View dnf5 log"), group);
    viewDnf5LogButton->setMinimumWidth(320);
    connect(viewDnf5LogButton, &QPushButton::clicked, this, &MainWindow::viewDnf5Log);
    groupLayout->addWidget(viewDnf5LogButton);

    auto *autoremoveButton = new QPushButton(i18n("Clean unused packages (dnf5 autoremove)"), group);
    autoremoveButton->setMinimumWidth(320);
    connect(autoremoveButton, &QPushButton::clicked, this, &MainWindow::cleanupUnusedPackages);
    groupLayout->addWidget(autoremoveButton);

    auto *viewCrashButton = new QPushButton(i18n("View software crash information"), group);
    viewCrashButton->setMinimumWidth(320);
    connect(viewCrashButton, &QPushButton::clicked, this, &MainWindow::viewCrashInfo);
    groupLayout->addWidget(viewCrashButton);

    auto *collectLogsButton = new QPushButton(i18n("Collect system logs"), group);
    collectLogsButton->setMinimumWidth(320);
    connect(collectLogsButton, &QPushButton::clicked, this, &MainWindow::collectSystemLogs);
    groupLayout->addWidget(collectLogsButton);

    layout->addWidget(group);

    layout->addStretch();
    return page;
}

QWidget *MainWindow::buildExtrasTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *group = new QGroupBox(i18n("Additional components"), page);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(10);

    for (const ExtraComponent &component : std::as_const(m_components)) {
        groupLayout->addWidget(createComponentCard(component));
    }

    auto *refreshButton = new QPushButton(i18n("Refresh status"), group);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshComponentStates);
    groupLayout->addWidget(refreshButton);

    layout->addWidget(group);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createComponentCard(const ExtraComponent &component)
{
    auto *frame = new QFrame;
    frame->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(frame);

    auto *top = new QHBoxLayout;
    auto *title = new QLabel(component.name, frame);
    title->setWordWrap(true);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    top->addWidget(title, 1);
    top->addStretch();

    auto *status = new QLabel(i18n("Status: checking"), frame);
    status->setMinimumWidth(220);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    top->addWidget(status);
    layout->addLayout(top);

    auto *description = new QLabel(component.description, frame);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *packages = new QLabel(i18n("Packages: %1", component.packages.join(QStringLiteral(", "))), frame);
    packages->setWordWrap(true);
    packages->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(packages);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *install = new QPushButton(i18n("Install"), frame);
    auto *uninstall = new QPushButton(i18n("Uninstall"), frame);
    install->setMinimumWidth(96);
    uninstall->setMinimumWidth(96);
    buttons->addWidget(install);
    buttons->addWidget(uninstall);
    layout->addLayout(buttons);

    connect(install, &QPushButton::clicked, this, [this, component]() {
        runPackageOperation(component, QStringLiteral("install"));
    });
    connect(uninstall, &QPushButton::clicked, this, [this, component]() {
        runPackageOperation(component, QStringLiteral("remove"));
    });

    m_componentWidgets.insert(component.id, {status, install, uninstall});
    return frame;
}

QWidget *MainWindow::buildEnvironmentTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *group = new QGroupBox(i18n("Environment variables"), page);
    auto *groupLayout = new QVBoxLayout(group);

    auto *hint = new QLabel(i18n("Set system-wide environment variables using the NAME=VALUE format. Changes are written to /etc/environment and take effect after logging in again."), group);
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);

    m_environmentList = new QListWidget(group);
    m_environmentList->setSelectionMode(QAbstractItemView::SingleSelection);
    groupLayout->addWidget(m_environmentList, 1);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(QStringLiteral("+"), group);
    auto *editButton = new QPushButton(i18n("Edit"), group);
    auto *removeButton = new QPushButton(QStringLiteral("−"), group);
    auto *reloadButton = new QPushButton(i18n("Reload"), group);
    auto *restoreDefaultsButton = new QPushButton(i18n("Restore defaults"), group);
    auto *applyButton = new QPushButton(i18n("Apply"), group);
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(removeButton);
    buttons->addStretch();
    buttons->addWidget(reloadButton);
    buttons->addWidget(restoreDefaultsButton);
    buttons->addWidget(applyButton);
    groupLayout->addLayout(buttons);

    m_environmentStatus = new QLabel(i18n("Log out and log back in for changes to take effect."), group);
    m_environmentStatus->setWordWrap(true);
    groupLayout->addWidget(m_environmentStatus);

    connect(addButton, &QPushButton::clicked, this, &MainWindow::addEnvironmentVariable);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editEnvironmentVariable);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeEnvironmentVariable);
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::loadEnvironmentVariables);
    connect(restoreDefaultsButton, &QPushButton::clicked, this, &MainWindow::restoreDefaultEnvironmentVariables);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyEnvironmentVariables);

    layout->addWidget(group);
    return page;
}

// ── Sleep tab ──

static const QString SLEEP_CONFIG_PATH = QStringLiteral("/etc/systemd/sleep.conf.d/90-miryu-toolkit.conf");

QString MainWindow::readSysFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll().trimmed());
}

QString MainWindow::readKernelConfig()
{
    // 1. /proc/config.gz (requires CONFIG_IKCONFIG_PROC=y)
    {
        QFile gz(QStringLiteral("/proc/config.gz"));
        if (gz.exists()) {
            QProcess proc;
            proc.start(QStringLiteral("zcat"), {QStringLiteral("/proc/config.gz")});
            if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
                const QString out = QString::fromUtf8(proc.readAllStandardOutput());
                if (!out.isEmpty()) {
                    return out;
                }
            }
        }
    }

    const QString release = readSysFile(QStringLiteral("/proc/sys/kernel/osrelease"));

    // 2. /boot/config-<release> (most distributions)
    if (!release.isEmpty()) {
        QFile f(QStringLiteral("/boot/config-") + release);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString::fromUtf8(f.readAll());
        }
    }

    // 3. /lib/modules/<release>/build/.config (kernel build tree)
    if (!release.isEmpty()) {
        QFile f(QStringLiteral("/lib/modules/") + release + QStringLiteral("/build/.config"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString::fromUtf8(f.readAll());
        }
    }

    return {};
}

bool MainWindow::kernelConfigEnabled(const QString &configText, const QString &key)
{
    if (configText.isEmpty()) {
        return true; // unknown -> assume available, don't disable the option
    }
    const QRegularExpression re(QStringLiteral("^%1=(y|m)$").arg(QRegularExpression::escape(key)),
                                QRegularExpression::MultilineOption);
    return re.match(configText).hasMatch();
}

QWidget *MainWindow::buildSleepTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // ── Allowed sleep modes ──
    auto *modesGroup = new QGroupBox(i18n("Allowed sleep modes"), page);
    auto *modesLayout = new QVBoxLayout(modesGroup);
    modesLayout->setSpacing(8);

    auto *modesHint = new QLabel(i18n("Choose the sleep modes you want to allow."), modesGroup);
    modesHint->setWordWrap(true);
    modesLayout->addWidget(modesHint);

    auto makeModeRow = [modesGroup](const QString &title, const QString &description, QCheckBox **check) {
        auto *row = new QHBoxLayout();
        row->setSpacing(12);
        auto *text = new QVBoxLayout();
        text->setSpacing(2);
        auto *titleLabel = new QLabel(title, modesGroup);
        QFont titleFont = titleLabel->font();
        titleFont.setBold(true);
        titleLabel->setFont(titleFont);
        text->addWidget(titleLabel);
        auto *descLabel = new QLabel(description, modesGroup);
        descLabel->setWordWrap(true);
        // Muted secondary text. Resolve the color through the palette role at
        // paint time so it stays readable when the color scheme changes;
        // palette(mid) is nearly invisible in both light and dark schemes.
        descLabel->setForegroundRole(QPalette::PlaceholderText);
        text->addWidget(descLabel);
        row->addLayout(text, 1);
        *check = new QCheckBox(modesGroup);
        row->addWidget(*check, 0, Qt::AlignVCenter);
        return row;
    };

    modesLayout->addLayout(makeModeRow(
        i18n("Suspend"),
        i18n("Keeps the session in memory (RAM). The computer is still running but uses only a bit of power."),
        &m_sleepSuspendCheck));
    modesLayout->addLayout(makeModeRow(
        i18n("Hibernate"),
        i18n("Saves the session to disk (Swap). Shuts down the computer."),
        &m_sleepHibernateCheck));
    modesLayout->addLayout(makeModeRow(
        i18n("Suspend then hibernate"),
        i18n("Suspends for a while (systemd decides how long, based on battery power), then hibernates."),
        &m_sleepSuspendThenHibernateCheck));
    modesLayout->addLayout(makeModeRow(
        i18n("Hybrid sleep"),
        i18n("Suspends but also saves the session to disk (Swap)."),
        &m_sleepHybridSleepCheck));

    layout->addWidget(modesGroup);

    // ── Suspend method ──
    auto *suspendMethodGroup = new QGroupBox(i18n("Suspend method"), page);
    auto *suspendMethodLayout = new QVBoxLayout(suspendMethodGroup);
    auto *suspendMethodHint = new QLabel(
        i18n("This is the method used to suspend the computer. Only the methods offered by your BIOS/firmware can be chosen."),
        suspendMethodGroup);
    suspendMethodHint->setWordWrap(true);
    suspendMethodLayout->addWidget(suspendMethodHint);
    auto *suspendMethodRow = new QHBoxLayout();
    suspendMethodRow->setSpacing(12);
    m_suspendModeCombo = new QComboBox(suspendMethodGroup);
    m_suspendModeCombo->setMinimumWidth(260);
    suspendMethodRow->addStretch();
    suspendMethodRow->addWidget(m_suspendModeCombo);
    suspendMethodLayout->addLayout(suspendMethodRow);
    layout->addWidget(suspendMethodGroup);

    // ── Hibernation method ──
    auto *hibernateMethodGroup = new QGroupBox(i18n("Hibernation method"), page);
    auto *hibernateMethodLayout = new QVBoxLayout(hibernateMethodGroup);
    auto *hibernateMethodHint = new QLabel(
        i18n("This is the method used to power off the computer during hibernation."),
        hibernateMethodGroup);
    hibernateMethodHint->setWordWrap(true);
    hibernateMethodLayout->addWidget(hibernateMethodHint);
    auto *hibernateMethodRow = new QHBoxLayout();
    hibernateMethodRow->setSpacing(12);
    m_hibernateMethodCombo = new QComboBox(hibernateMethodGroup);
    m_hibernateMethodCombo->setMinimumWidth(260);
    hibernateMethodRow->addStretch();
    hibernateMethodRow->addWidget(m_hibernateMethodCombo);
    hibernateMethodLayout->addLayout(hibernateMethodRow);
    layout->addWidget(hibernateMethodGroup);

    // ── Hibernation image compressor ──
    auto *compressorGroup = new QGroupBox(i18n("Hibernation image compressor"), page);
    auto *compressorLayout = new QVBoxLayout(compressorGroup);
    auto *compressorHint = new QLabel(
        i18n("The algorithm used to compress the hibernation image. The kernel must have the corresponding compression support built in; algorithms that are not available are greyed out."),
        compressorGroup);
    compressorHint->setWordWrap(true);
    compressorLayout->addWidget(compressorHint);
    auto *compressorRow = new QHBoxLayout();
    compressorRow->setSpacing(12);
    m_hibernateCompressorCombo = new QComboBox(compressorGroup);
    m_hibernateCompressorCombo->setMinimumWidth(260);
    compressorRow->addStretch();
    compressorRow->addWidget(m_hibernateCompressorCombo);
    compressorLayout->addLayout(compressorRow);
    layout->addWidget(compressorGroup);

    // ── Detected problems ──
    auto *problemsGroup = new QGroupBox(i18n("Detected problems"), page);
    auto *problemsLayout = new QVBoxLayout(problemsGroup);
    auto *problemsHint = new QLabel(i18n("Fix the detected problems to make your allowed sleep modes available."), problemsGroup);
    problemsHint->setWordWrap(true);
    problemsLayout->addWidget(problemsHint);
    m_sleepProblemsList = new QListWidget(problemsGroup);
    m_sleepProblemsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_sleepProblemsList->setAlternatingRowColors(true);
    m_sleepProblemsList->setMinimumHeight(140);
    problemsLayout->addWidget(m_sleepProblemsList, 1);
    auto *fixButtonLayout = new QHBoxLayout();
    fixButtonLayout->addStretch();
    m_sleepFixButton = new QPushButton(i18n("Fix detected problems"), problemsGroup);
    m_sleepFixButton->setIcon(QIcon::fromTheme(QStringLiteral("tools-wizard")));
    m_sleepFixButton->setEnabled(false);
    connect(m_sleepFixButton, &QPushButton::clicked, this, &MainWindow::fixSleepProblems);
    fixButtonLayout->addWidget(m_sleepFixButton);
    problemsLayout->addLayout(fixButtonLayout);
    layout->addWidget(problemsGroup);

    // ── Available sleep modes ──
    auto *availableGroup = new QGroupBox(i18n("Available sleep modes"), page);
    auto *availableLayout = new QVBoxLayout(availableGroup);
    auto *availableHint = new QLabel(i18n("These are the sleep modes systemd reports as available right now."), availableGroup);
    availableHint->setWordWrap(true);
    availableLayout->addWidget(availableHint);
    m_sleepAvailableList = new QListWidget(availableGroup);
    m_sleepAvailableList->setSelectionMode(QAbstractItemView::NoSelection);
    m_sleepAvailableList->setAlternatingRowColors(true);
    m_sleepAvailableList->setMinimumHeight(96);
    availableLayout->addWidget(m_sleepAvailableList, 1);
    layout->addWidget(availableGroup);

    // ── Bottom: status + apply ──
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);
    m_sleepStatusLabel = new QLabel(i18n("Saving requires administrator authentication through polkit."), page);
    m_sleepStatusLabel->setWordWrap(true);
    m_sleepApplyButton = new QPushButton(i18n("Apply sleep settings"), page);
    m_sleepApplyButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    connect(m_sleepApplyButton, &QPushButton::clicked, this, &MainWindow::saveSleepConfig);
    bottomLayout->addWidget(m_sleepStatusLabel, 1);
    bottomLayout->addWidget(m_sleepApplyButton);
    layout->addLayout(bottomLayout);

    layout->addStretch();
    return page;
}

void MainWindow::loadSleepConfig()
{
    if (!m_sleepSuspendCheck) {
        return;
    }

    // Read the drop-in config written by this tool (if any)
    QVariantMap values;
    QFile file(SLEEP_CONFIG_PATH);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('['))) {
                continue;
            }
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
            }
        }
    }

    auto isAllowed = [&values](const QString &key) -> bool {
        const QString v = values.value(key, QStringLiteral("yes")).toString().toLower();
        return !(v == QLatin1String("no") || v == QLatin1String("false")
                 || v == QLatin1String("0") || v == QLatin1String("off"));
    };

    m_sleepSuspendCheck->setChecked(isAllowed(QStringLiteral("AllowSuspend")));
    m_sleepHibernateCheck->setChecked(isAllowed(QStringLiteral("AllowHibernation")));
    m_sleepSuspendThenHibernateCheck->setChecked(isAllowed(QStringLiteral("AllowSuspendThenHibernate")));
    m_sleepHybridSleepCheck->setChecked(isAllowed(QStringLiteral("AllowHybridSleep")));

    // Populate suspend mode combo from /sys/power/mem_sleep
    const QString memSleep = readSysFile(QStringLiteral("/sys/power/mem_sleep"));
    const QString suspendCurrent = values.value(QStringLiteral("MemorySleepMode"), QString()).toString();
    m_suspendModeCombo->clear();
    m_suspendModeCombo->addItem(i18n("Default"), QString());
    if (!memSleep.isEmpty()) {
        const QStringList tokens = memSleep.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (QString token : tokens) {
            bool active = false;
            if (token.startsWith(QLatin1Char('[')) && token.endsWith(QLatin1Char(']'))) {
                token = token.mid(1, token.size() - 2);
                active = true;
            }
            QString label;
            if (token == QLatin1String("s2idle")) {
                label = i18n("Suspend-to-idle (s2idle)");
            } else if (token == QLatin1String("deep")) {
                label = i18n("Suspend-to-RAM (S3)");
            } else if (token == QLatin1String("shallow")) {
                label = i18n("Shallow (S1)");
            } else {
                label = token;
            }
            m_suspendModeCombo->addItem(label, token);
            if (active && suspendCurrent.isEmpty()) {
                // Show the kernel default in the "Default" entry when nothing is configured
                const int idx = m_suspendModeCombo->findData(QString());
                if (idx >= 0) {
                    m_suspendModeCombo->setItemText(idx, i18n("Default — %1", label));
                }
            }
        }
    }
    const int suspendIdx = m_suspendModeCombo->findData(suspendCurrent);
    m_suspendModeCombo->setCurrentIndex(suspendIdx >= 0 ? suspendIdx : 0);

    // Populate hibernate method combo from /sys/power/disk
    const QString disk = readSysFile(QStringLiteral("/sys/power/disk"));
    const QString hibernateCurrent = values.value(QStringLiteral("HibernateMode"), QString()).toString();
    m_hibernateMethodCombo->clear();
    m_hibernateMethodCombo->addItem(i18n("Default"), QString());
    if (!disk.isEmpty()) {
        const QStringList tokens = disk.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (QString token : tokens) {
            bool active = false;
            if (token.startsWith(QLatin1Char('[')) && token.endsWith(QLatin1Char(']'))) {
                token = token.mid(1, token.size() - 2);
                active = true;
            }
            QString label;
            if (token == QLatin1String("platform")) {
                label = i18n("Platform (the firmware powers off)");
            } else if (token == QLatin1String("shutdown")) {
                label = i18n("Shutdown (the kernel powers off)");
            } else {
                // reboot, suspend, test_resume are not for normal use
                continue;
            }
            m_hibernateMethodCombo->addItem(label, token);
            if (active && hibernateCurrent.isEmpty()) {
                const int idx = m_hibernateMethodCombo->findData(QString());
                if (idx >= 0) {
                    m_hibernateMethodCombo->setItemText(idx, i18n("Default — %1", label));
                }
            }
        }
    }
    const int hibernateIdx = m_hibernateMethodCombo->findData(hibernateCurrent);
    m_hibernateMethodCombo->setCurrentIndex(hibernateIdx >= 0 ? hibernateIdx : 0);

    // Populate hibernation compressor combo.
    //
    // The kernel picks the compressor at boot via the "hibernate.compressor="
    // command line parameter (hardcoded default: "lzo"). Miryu Toolkit's
    // default recommendation is lz4, falling back to lzo when lz4 is missing.
    // Algorithms that are not available at runtime are greyed out.
    bool lzoAvailable = false;
    bool lz4Available = false;
    QFile cryptoFile(QStringLiteral("/proc/crypto"));
    if (cryptoFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&cryptoFile);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.startsWith(QStringLiteral("name")) && line.contains(QLatin1Char(':'))) {
                const QString name = line.section(QLatin1Char(':'), 1).trimmed();
                if (name == QLatin1String("lzo")) lzoAvailable = true;
                else if (name == QLatin1String("lz4")) lz4Available = true;
            }
        }
    }
    // Compile-time kernel config as a fallback — it tells us whether the
    // module can be loaded at all even if /proc/crypto isn't populated yet.
    if (!lzoAvailable || !lz4Available) {
        const QString kernelCfg = readKernelConfig();
        if (!lzoAvailable) lzoAvailable = kernelConfigEnabled(kernelCfg, QStringLiteral("CONFIG_CRYPTO_LZO"));
        if (!lz4Available) lz4Available = kernelConfigEnabled(kernelCfg, QStringLiteral("CONFIG_CRYPTO_LZ4"));
    }

    // Prefer lz4 (Miryu's default recommendation), fall back to lzo.
    QString defaultLabel;
    if (lz4Available) {
        defaultLabel = i18n("Default — LZ4");
    } else if (lzoAvailable) {
        defaultLabel = i18n("Default — LZO");
    } else {
        defaultLabel = i18n("Default — none available");
    }

    const QString compressorCurrent = values.value(QStringLiteral("HibernateCompressor"), QString()).toString();
    m_hibernateCompressorCombo->clear();
    m_hibernateCompressorCombo->addItem(defaultLabel, QString());
    m_hibernateCompressorCombo->addItem(QStringLiteral("LZO"), QStringLiteral("lzo"));
    m_hibernateCompressorCombo->addItem(QStringLiteral("LZ4"), QStringLiteral("lz4"));

    // Grey out algorithms that are not built into or loadable by the kernel.
    auto *compModel = qobject_cast<QStandardItemModel *>(m_hibernateCompressorCombo->model());
    if (compModel) {
        for (int i = 0; i < m_hibernateCompressorCombo->count(); ++i) {
            const QString data = m_hibernateCompressorCombo->itemData(i).toString();
            bool enabled = true;
            if (data == QLatin1String("lzo")) {
                enabled = lzoAvailable;
            } else if (data == QLatin1String("lz4")) {
                enabled = lz4Available;
            }
            QStandardItem *item = compModel->item(i);
            if (item) {
                item->setEnabled(enabled);
            }
        }
        // The "Default" entry is only meaningful when at least one algo is
        // available. Otherwise both underlying choices are disabled and the
        // whole section should be considered unusable.
        QStandardItem *defaultItem = compModel->item(0);
        if (defaultItem) {
            defaultItem->setEnabled(lzoAvailable || lz4Available);
        }
    }

    int compIdx = m_hibernateCompressorCombo->findData(compressorCurrent);
    // If the stored value points to a disabled item, fall back to Default.
    if (compIdx >= 0) {
        QStandardItem *sel = compModel ? compModel->item(compIdx) : nullptr;
        if (sel && !sel->isEnabled()) {
            compIdx = 0;
        }
    }
    m_hibernateCompressorCombo->setCurrentIndex(compIdx >= 0 ? compIdx : 0);

    // Connect signals after the UI is populated
    connect(m_sleepSuspendCheck, &QCheckBox::toggled, this, [this](bool) {
        updateSleepDependents();
        refreshSleepChecks();
    });
    connect(m_sleepHibernateCheck, &QCheckBox::toggled, this, [this](bool) {
        updateHibernateMethodSensitivity();
        refreshSleepChecks();
    });
    connect(m_sleepSuspendThenHibernateCheck, &QCheckBox::toggled, this, [this](bool) {
        updateHibernateMethodSensitivity();
        refreshSleepChecks();
    });
    connect(m_sleepHybridSleepCheck, &QCheckBox::toggled, this, [this](bool) {
        refreshSleepChecks();
    });

    updateSleepDependents();
    refreshSleepChecks();
}

void MainWindow::updateSleepDependents()
{
    const bool suspendAllowed = m_sleepSuspendCheck->isChecked();
    // Suspend-then-hibernate and hybrid sleep require suspend
    if (!suspendAllowed) {
        m_sleepSuspendThenHibernateCheck->setChecked(false);
    }
    m_sleepSuspendThenHibernateCheck->setEnabled(suspendAllowed);
    m_sleepHybridSleepCheck->setEnabled(suspendAllowed);

    // Suspend method is only relevant when more than one method is offered
    const int suspendMethods = m_suspendModeCombo ? m_suspendModeCombo->count() - 1 : 0;
    if (m_suspendModeCombo) {
        m_suspendModeCombo->setEnabled(suspendAllowed && suspendMethods > 1);
    }

    updateHibernateMethodSensitivity();
}

void MainWindow::updateHibernateMethodSensitivity()
{
    const bool hibernates = m_sleepHibernateCheck->isChecked()
                            || m_sleepSuspendThenHibernateCheck->isChecked();
    const int hibernateMethods = m_hibernateMethodCombo ? m_hibernateMethodCombo->count() - 1 : 0;
    if (m_hibernateMethodCombo) {
        m_hibernateMethodCombo->setEnabled(hibernates && hibernateMethods > 1);
    }
    if (m_hibernateCompressorCombo) {
        m_hibernateCompressorCombo->setEnabled(hibernates);
    }
}

void MainWindow::refreshSleepChecks()
{
    if (!m_sleepProblemsList) {
        return;
    }

    m_sleepProblemsList->clear();

    const bool suspend = m_sleepSuspendCheck->isChecked();
    const bool hibernate = m_sleepHibernateCheck->isChecked()
                           || m_sleepSuspendThenHibernateCheck->isChecked()
                           || m_sleepHybridSleepCheck->isChecked();

    struct Report {
        int ok; // 1 = ok, 0 = problem, -1 = information
        QString title;
        QString detail;
    };
    QList<Report> reports;
    bool hasFixableProblem = false;

    // ── Suspend check ──
    if (suspend) {
        const QString state = readSysFile(QStringLiteral("/sys/power/state"));
        const QStringList states = state.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (states.contains(QStringLiteral("mem")) || states.contains(QStringLiteral("freeze"))) {
            const QString memSleep = readSysFile(QStringLiteral("/sys/power/mem_sleep"));
            QString current;
            for (const QString &t : memSleep.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
                    current = t.mid(1, t.size() - 2);
                    break;
                }
            }
            if (current.isEmpty()) {
                reports.append({1, i18n("Suspend"), i18n("The kernel supports suspend.")});
            } else {
                QString label = current;
                if (current == QLatin1String("s2idle")) {
                    label = i18n("Suspend-to-idle (s2idle)");
                } else if (current == QLatin1String("deep")) {
                    label = i18n("Suspend-to-RAM (S3)");
                } else if (current == QLatin1String("shallow")) {
                    label = i18n("Shallow (S1)");
                }
                reports.append({1, i18n("Suspend"), i18n("The kernel supports suspend. Method in use: %1.", label)});
            }
        } else {
            reports.append({0, i18n("Suspend"), i18n("The kernel does not support suspend.")});
        }
    }

    // ── Hibernation checks ──
    if (hibernate) {
        // Kernel support
        const QString state = readSysFile(QStringLiteral("/sys/power/state"));
        if (state.split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(QStringLiteral("disk"))) {
            reports.append({1, i18n("Kernel support"), i18n("The kernel supports hibernation.")});
        } else {
            reports.append({0, i18n("Kernel support"), i18n("The kernel does not support hibernation.")});
        }

        // Secure boot / kernel lockdown
        const QString lockdown = readSysFile(QStringLiteral("/sys/kernel/security/lockdown"));
        bool locked = false;
        const QRegularExpression re(QStringLiteral(R"(\[(\w+)\])"));
        auto m = re.match(lockdown);
        if (m.hasMatch()) {
            locked = m.captured(1) != QLatin1String("none");
        }
        bool secureBoot = false;
        const QString efivarsPath = QStringLiteral("/sys/firmware/efi/efivars");
        QDir efivars(efivarsPath);
        if (efivars.exists()) {
            const QStringList entries = efivars.entryList(QDir::Files);
            for (const QString &name : entries) {
                if (name.startsWith(QStringLiteral("SecureBoot-"))) {
                    QFile f(efivars.filePath(name));
                    if (f.open(QIODevice::ReadOnly)) {
                        const QByteArray data = f.readAll();
                        if (!data.isEmpty()) {
                            secureBoot = data.at(data.size() - 1) == 1;
                        }
                    }
                    break;
                }
            }
        }
        if (locked) {
            reports.append({0, i18n("Secure Boot"), i18n("Secure Boot is enabled and the kernel lockdown prevents hibernation.")});
        } else if (secureBoot) {
            reports.append({1, i18n("Secure Boot"), i18n("Secure Boot is enabled but the kernel lockdown is not active.")});
        } else {
            reports.append({1, i18n("Secure Boot"), i18n("Secure Boot is disabled.")});
        }

        // Swap areas
        struct SwapEntry {
            QString path;
            bool swapfile;
            qint64 size; // bytes
            int priority;
        };
        QList<SwapEntry> swapEntries;
        QFile swaps(QStringLiteral("/proc/swaps"));
        if (swaps.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&swaps);
            QString line = stream.readLine(); // header
            while (!stream.atEnd()) {
                line = stream.readLine().trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                const QStringList fields = line.split(QRegularExpression(QStringLiteral(R"(\s+)")));
                if (fields.size() < 5) {
                    continue;
                }
                QString path = fields.at(0);
                path.replace(QStringLiteral("\\040"), QStringLiteral(" "));
                const QString type = fields.at(1);
                if (type == QLatin1String("file")) {
                    if (path.endsWith(QStringLiteral(" (deleted)"))) {
                        continue;
                    }
                } else if (type == QLatin1String("partition")) {
                    if (QFileInfo(path).fileName().startsWith(QStringLiteral("zram"))) {
                        continue;
                    }
                } else {
                    continue;
                }
                bool ok = false;
                const qint64 sizeKb = fields.at(2).toLongLong(&ok);
                if (!ok) {
                    continue;
                }
                bool priOk = false;
                const int priority = fields.value(4).toInt(&priOk);
                swapEntries.append({path, type == QLatin1String("file"), sizeKb * 1024, priOk ? priority : -2});
            }
        }

        qint64 ram = 0;
        QFile meminfo(QStringLiteral("/proc/meminfo"));
        if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&meminfo);
            while (!stream.atEnd()) {
                const QString line = stream.readLine();
                if (line.startsWith(QStringLiteral("MemTotal:"))) {
                    const QRegularExpression re(QStringLiteral(R"(MemTotal:\s+(\d+)\s+kB)"));
                    auto mm = re.match(line);
                    if (mm.hasMatch()) {
                        ram = mm.captured(1).toLongLong() * 1024;
                    }
                    break;
                }
            }
        }

        auto formatSize = [](qint64 bytes) -> QString {
            static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
            double size = bytes;
            int unit = 0;
            while (size >= 1024.0 && unit < 4) {
                size /= 1024.0;
                ++unit;
            }
            return QString::number(size, 'f', 1) + QStringLiteral(" ") + QString::fromUtf8(units[unit]);
        };

        if (swapEntries.isEmpty()) {
            reports.append({0, i18n("Swap"), i18n("No swap partition or swap file is in use.")});
            hasFixableProblem = true;
        } else {
            for (const auto &entry : swapEntries) {
                const QString title = i18n("Swap area in %1", entry.path);
                const QString swapSize = formatSize(entry.size);
                const QString ramSize = formatSize(ram);
                if (entry.size < ram) {
                    reports.append({0, title, i18n("The swap area (%1) is smaller than the memory (%2).", swapSize, ramSize)});
                    hasFixableProblem = true;
                } else {
                    reports.append({-1, title, i18n("The swap area (%1) is at least as large as the memory (%2).", swapSize, ramSize)});
                }
            }

            // Hibernation uses the swap area with the highest priority. If a
            // zram device outranks the swap file, the kernel tries to write the
            // hibernation image to zram and fails with EBUSY.
            int highestSwapFilePriority = -3;
            for (const auto &entry : swapEntries) {
                if (entry.swapfile && entry.priority > highestSwapFilePriority) {
                    highestSwapFilePriority = entry.priority;
                }
            }
            int highestZramPriority = -3;
            QFile swapsFile(QStringLiteral("/proc/swaps"));
            if (swapsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&swapsFile);
                stream.readLine(); // header
                while (!stream.atEnd()) {
                    const QStringList f = stream.readLine().trimmed().split(QRegularExpression(QStringLiteral(R"(\s+)")));
                    if (f.size() >= 5 && f.at(0).contains(QStringLiteral("zram"))) {
                        bool ok = false;
                        const int p = f.at(4).toInt(&ok);
                        if (ok && p > highestZramPriority) {
                            highestZramPriority = p;
                        }
                    }
                }
            }
            if (highestZramPriority > highestSwapFilePriority && highestSwapFilePriority >= 0) {
                reports.append({0, i18n("Swap priority"),
                                i18n("A zram device (priority %1) outranks the swap file (priority %2); hibernation may fail with \"device or resource busy\".",
                                     QString::number(highestZramPriority), QString::number(highestSwapFilePriority))});
                hasFixableProblem = true;
            } else if (highestSwapFilePriority >= 0) {
                reports.append({-1, i18n("Swap priority"),
                                i18n("The swap file has the highest swap priority (%1).", QString::number(highestSwapFilePriority))});
            }
        }

        // Hibernation image compression. The kernel refuses to hibernate with
        // "Operation not supported" when the configured compressor is missing.
        // If the kernel offers both lzo and lz4 there is nothing to report —
        // the compressor selector above covers the choice. We only surface
        // something when neither is available (red), or only one is (blue).
        QFile compParam(QStringLiteral("/sys/module/hibernate/parameters/compressor"));
        if (compParam.exists()) {
            QString compressor = QStringLiteral("lzo");
            if (compParam.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString value = QString::fromUtf8(compParam.readAll()).trimmed();
                if (!value.isEmpty()) {
                    compressor = value;
                }
            }

            QStringList availableAlgs;
            QFile crypto(QStringLiteral("/proc/crypto"));
            if (crypto.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&crypto);
                while (!stream.atEnd()) {
                    const QString line = stream.readLine().trimmed();
                    if (line.startsWith(QStringLiteral("name")) && line.contains(QLatin1Char(':'))) {
                        const QString name = line.section(QLatin1Char(':'), 1).trimmed();
                        if (name == QLatin1String("lzo") || name == QLatin1String("lz4")) {
                            availableAlgs.append(name);
                        }
                    }
                }
            }

            const bool hasLzo = availableAlgs.contains(QLatin1String("lzo"));
            const bool hasLz4 = availableAlgs.contains(QLatin1String("lz4"));

            if (!hasLzo && !hasLz4) {
                reports.append({0, i18n("Hibernation compression"),
                                i18n("Neither the lzo nor the lz4 hibernation compressor is available in the kernel; hibernation fails with \"Operation not supported\".")});
                hasFixableProblem = true;
            } else if (hasLzo && hasLz4) {
                // Both algorithms are available — nothing to warn about.
            } else {
                // Only one of them is available. Tell the user to pick it.
                const QString working = hasLzo ? QStringLiteral("lzo") : QStringLiteral("lz4");
                if (availableAlgs.contains(compressor)) {
                    reports.append({-1, i18n("Hibernation compression"),
                                    i18n("The hibernation compressor %1 is available.", compressor)});
                } else {
                    reports.append({-1, i18n("Hibernation compression"),
                                    i18n("The configured compressor %1 is not available; select %2 in the compressor list above.",
                                         compressor, working)});
                }
            }
        }

        // Hyper-V: uio_hv_generic has no suspend callback, so vmbus_suspend()
        // returns EOPNOTSUPP and the freezer aborts hibernation.
        const QString uioDriverDir = QStringLiteral("/sys/bus/vmbus/drivers/uio_hv_generic");
        if (QDir(uioDriverDir).exists()) {
            QStringList boundDevices;
            const QFileInfoList entries = QDir(uioDriverDir).entryInfoList(
                QDir::NoDotAndDotDot | QDir::Dirs | QDir::System);
            for (const QFileInfo &entry : entries) {
                if (entry.isSymLink()) {
                    boundDevices.append(entry.fileName());
                }
            }
            if (!boundDevices.isEmpty()) {
                reports.append({0, i18n("Hyper-V devices"),
                                i18n("The uio_hv_generic driver is bound to %1 VMBus device(s) and has no suspend support, which aborts hibernation.",
                                     QString::number(boundDevices.size()))});
                hasFixableProblem = true;
            } else {
                reports.append({-1, i18n("Hyper-V devices"),
                                i18n("No VMBus device is bound to uio_hv_generic.")});
            }
        }

        // VMware: vmw_pm_freeze() returns EBUSY while any user-space DRM
        // client holds a context, even with 3D acceleration disabled.
        const QString vmwgfxDir = QStringLiteral("/sys/bus/pci/drivers/vmwgfx");
        if (QDir(vmwgfxDir).exists()) {
            QStringList boundGpus;
            const QFileInfoList entries = QDir(vmwgfxDir).entryInfoList(
                QDir::NoDotAndDotDot | QDir::Dirs | QDir::System);
            for (const QFileInfo &entry : entries) {
                if (entry.isSymLink()) {
                    boundGpus.append(entry.fileName());
                }
            }
            const bool graphicalSession = qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
                                           || qEnvironmentVariableIsSet("DISPLAY");
            if (!boundGpus.isEmpty() && graphicalSession) {
                reports.append({0, i18n("VMware graphics"),
                                i18n("The vmwgfx driver refuses to freeze while a graphical session holds a DRM context, which aborts hibernation.")});
                hasFixableProblem = true;
            } else {
                reports.append({-1, i18n("VMware graphics"),
                                i18n("The vmwgfx driver will be released during hibernation.")});
            }
        }

        // Drivers whose freeze callback returns an error abort hibernation.
        // The helper learns them from the kernel log and the sleep hook detaches
        // them for the duration of the hibernation.
        const QString sleepReleaseConf = QStringLiteral("/etc/miryu-toolkit/sleep-release.conf");
        QFile releaseFile(sleepReleaseConf);
        QStringList releasedDrivers;
        if (releaseFile.exists() && releaseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!releaseFile.atEnd()) {
                const QString line = QString::fromUtf8(releaseFile.readLine()).trimmed();
                if (!line.isEmpty()) {
                    releasedDrivers.append(line);
                }
            }
            releaseFile.close();
        }
        if (!releasedDrivers.isEmpty()) {
            reports.append({-1, i18n("Hibernation drivers"),
                            i18n("%1 driver(s) abort the freezer and are detached while the image is written: %2",
                                 QString::number(releasedDrivers.size()), releasedDrivers.join(QStringLiteral(", ")))});
        }

        QProcess hibernateState;
        hibernateState.start(QStringLiteral("systemctl"),
                             {QStringLiteral("is-failed"), QStringLiteral("systemd-hibernate.service")});
        if (hibernateState.waitForFinished(3000) && hibernateState.exitCode() == 0) {
            reports.append({0, i18n("Last hibernation"),
                            i18n("The previous hibernation attempt failed; the failing driver can be released automatically.")});
            hasFixableProblem = true;
        }

        // Resume device
        const QString resume = readSysFile(QStringLiteral("/sys/power/resume"));
        QStringList resumeParts = resume.split(QLatin1Char(':'));
        unsigned long resumeMajor = 0, resumeMinor = 0;
        if (resumeParts.size() == 2) {
            bool ok1 = false, ok2 = false;
            resumeMajor = resumeParts.at(0).toULong(&ok1);
            resumeMinor = resumeParts.at(1).toULong(&ok2);
            if (!ok1 || !ok2) {
                resumeMajor = resumeMinor = 0;
            }
        }

        if (resumeMajor == 0) {
            reports.append({0, i18n("Resume device"), i18n("No resume device is set.")});
            hasFixableProblem = true;
        } else {
            // Try to match the resume device to a swap area
            QString matchedPath;
            for (const auto &entry : swapEntries) {
                struct stat st;
                if (::stat(entry.path.toUtf8().constData(), &st) != 0) {
                    continue;
                }
                const dev_t rdev = entry.swapfile ? st.st_dev : st.st_rdev;
                if (static_cast<int>(major(rdev)) == static_cast<int>(resumeMajor)
                    && static_cast<int>(minor(rdev)) == static_cast<int>(resumeMinor)) {
                    matchedPath = entry.path;
                    break;
                }
            }
            if (matchedPath.isEmpty()) {
                reports.append({0, i18n("Resume device"),
                                i18n("The resume device (%1:%2) does not match any swap partition or swap file in use.",
                                     QString::number(resumeMajor), QString::number(resumeMinor))});
                hasFixableProblem = true;
            } else {
                reports.append({-1, i18n("Resume device"),
                                i18n("The resume device is set to %1.", matchedPath)});
            }
        }

        // SELinux: under enforcing mode, swap files must carry the swapfile_t
        // label, otherwise hibernation (resume) is denied by the policy.
        bool selinuxEnforcing = false;
        QFile enforceFile(QStringLiteral("/sys/fs/selinux/enforce"));
        if (enforceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString val = QString::fromUtf8(enforceFile.readAll()).trimmed();
            selinuxEnforcing = (val == QLatin1String("1"));
        }
        if (selinuxEnforcing) {
            for (const auto &entry : swapEntries) {
                if (!entry.swapfile) {
                    continue; // swap partitions do not need the swapfile_t label
                }
                QProcess statProc;
                statProc.start(QStringLiteral("stat"),
                               {QStringLiteral("-c"), QStringLiteral("%C"), entry.path});
                statProc.waitForFinished(2000);
                const QString ctx = QString::fromUtf8(statProc.readAllStandardOutput()).trimmed();
                if (!ctx.contains(QStringLiteral("swapfile_t"))) {
                    reports.append({0, i18n("SELinux"),
                                    i18n("The swap file %1 is not labeled with swapfile_t; hibernation may be denied under SELinux enforcing mode.",
                                         entry.path)});
                    hasFixableProblem = true;
                } else {
                    reports.append({-1, i18n("SELinux"),
                                    i18n("The swap file %1 is correctly labeled with swapfile_t.",
                                         entry.path)});
                }

                // systemd-logind must be able to search the swap file's
                // parent directory; label it root_t if it is not already
                // a logind-searchable type. Skip the root directory.
                const QString swapDir = QFileInfo(entry.path).path();
                if (swapDir != QStringLiteral("/")) {
                    QProcess dirProc;
                    dirProc.start(QStringLiteral("stat"),
                                  {QStringLiteral("-c"), QStringLiteral("%C"), swapDir});
                    dirProc.waitForFinished(2000);
                    const QString dirCtx = QString::fromUtf8(dirProc.readAllStandardOutput()).trimmed();
                    if (!dirCtx.contains(QStringLiteral("root_t"))
                        && !dirCtx.contains(QStringLiteral("default_t"))
                        && !dirCtx.contains(QStringLiteral("swapfile_t"))) {
                        reports.append({0, i18n("SELinux"),
                                        i18n("The swap directory %1 has an SELinux context that systemd-logind cannot search.",
                                             swapDir)});
                        hasFixableProblem = true;
                    } else {
                        reports.append({-1, i18n("SELinux"),
                                        i18n("The swap directory %1 has a searchable SELinux context.",
                                             swapDir)});
                    }
                }
            }

            // The domain_kernel_dump boolean must be on so the kernel is
            // allowed to write memory to the swap file for hibernation.
            QProcess boolProc;
            boolProc.start(QStringLiteral("getsebool"), {QStringLiteral("domain_kernel_dump")});
            boolProc.waitForFinished(2000);
            const QString boolOut = QString::fromUtf8(boolProc.readAllStandardOutput()).trimmed();
            if (!boolOut.endsWith(QStringLiteral("on"))) {
                reports.append({0, i18n("SELinux"),
                                i18n("The SELinux boolean domain_kernel_dump is off; hibernation to swap is not permitted.")});
                hasFixableProblem = true;
            } else {
                reports.append({-1, i18n("SELinux"),
                                i18n("The SELinux boolean domain_kernel_dump is on.")});
            }
        }
    }

    // Populate the problems list (show problems and information only; if none, show "No problems detected")
    QList<Report> shown;
    for (const auto &r : reports) {
        if (r.ok != 1) {
            shown.append(r);
        }
    }
    if (shown.isEmpty()) {
        shown.append({1, i18n("No problems detected"), i18n("This computer looks ready for the sleep modes above.")});
    }
    for (const auto &r : shown) {
        QString iconName;
        if (r.ok == 1) {
            iconName = QStringLiteral("emblem-ok-symbolic");
        } else if (r.ok == 0) {
            iconName = QStringLiteral("dialog-warning-symbolic");
        } else {
            iconName = QStringLiteral("dialog-information-symbolic");
        }
        auto *item = new QListWidgetItem(QIcon::fromTheme(iconName),
                                         r.title + QStringLiteral(": ") + r.detail);
        if (r.ok == 0) {
            item->setForeground(QColor(QStringLiteral("#c0392b")));
        }
        m_sleepProblemsList->addItem(item);
    }

    if (m_sleepFixButton) {
        m_sleepFixButton->setEnabled(hasFixableProblem);
    }

    // ── Available sleep modes (from systemd/logind) ──
    if (m_sleepAvailableList) {
        m_sleepAvailableList->clear();
        QStringList available;
        // Use busctl to query logind for available sleep modes
        struct ModeQuery {
            const char *method;
            const char *label;
        };
        const ModeQuery queries[] = {
            {"CanSuspend", QT_TRANSLATE_NOOP("MainWindow", "Suspend")},
            {"CanHibernate", QT_TRANSLATE_NOOP("MainWindow", "Hibernate")},
            {"CanSuspendThenHibernate", QT_TRANSLATE_NOOP("MainWindow", "Suspend then hibernate")},
            {"CanHybridSleep", QT_TRANSLATE_NOOP("MainWindow", "Hybrid sleep")},
        };
        for (const auto &q : queries) {
            QProcess busctl;
            busctl.start(QStringLiteral("busctl"),
                         QStringList() << QStringLiteral("call")
                                       << QStringLiteral("org.freedesktop.login1")
                                       << QStringLiteral("/org/freedesktop/login1")
                                       << QStringLiteral("org.freedesktop.login1.Manager")
                                       << QString::fromUtf8(q.method));
            if (busctl.waitForFinished(3000) && busctl.exitCode() == 0) {
                const QString output = QString::fromUtf8(busctl.readAllStandardOutput()).trimmed();
                if (output.contains(QStringLiteral("\"yes\"")) || output.contains(QStringLiteral("\"challenge\""))) {
                    available << i18n(q.label);
                }
            }
        }
        if (available.isEmpty()) {
            // Fallback: read kernel supported states from /sys/power/state
            const QString state = readSysFile(QStringLiteral("/sys/power/state"));
            const QStringList states = state.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (states.contains(QStringLiteral("mem")) || states.contains(QStringLiteral("freeze"))) {
                available << i18n("Suspend");
            }
            if (states.contains(QStringLiteral("disk"))) {
                available << i18n("Hibernate");
            }
        }
        if (available.isEmpty()) {
            auto *item = new QListWidgetItem(i18n("None"));
            m_sleepAvailableList->addItem(item);
        } else {
            for (const QString &mode : available) {
                m_sleepAvailableList->addItem(mode);
            }
        }
    }
}

void MainWindow::saveSleepConfig()
{
    if (m_runningProcess) {
        return;
    }

    // Build the sleep.conf content
    QStringList lines;
    lines << QStringLiteral("# Do not edit this file. It is generated by Miryu Toolkit.");
    lines << QStringLiteral("[Sleep]");
    lines << QStringLiteral("AllowSuspend=%1").arg(m_sleepSuspendCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("AllowHibernation=%1").arg(m_sleepHibernateCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("AllowSuspendThenHibernate=%1").arg(m_sleepSuspendThenHibernateCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"));
    lines << QStringLiteral("AllowHybridSleep=%1").arg(m_sleepHybridSleepCheck->isChecked() ? QStringLiteral("yes") : QStringLiteral("no"));

    const QString suspendMode = m_suspendModeCombo->currentData().toString();
    if (!suspendMode.isEmpty()) {
        lines << QStringLiteral("MemorySleepMode=%1").arg(suspendMode);
    }
    const QString hibernateMethod = m_hibernateMethodCombo->currentData().toString();
    if (!hibernateMethod.isEmpty()) {
        lines << QStringLiteral("HibernateMode=%1").arg(hibernateMethod);
    }
    const QString hibernateCompressor = m_hibernateCompressorCombo ? m_hibernateCompressorCombo->currentData().toString() : QString();
    lines << QStringLiteral("HibernateCompressor=%1").arg(hibernateCompressor);

    // Write to a temp file
    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/miryu-sleep-XXXXXX.conf"));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        KMessageBox::error(this, i18n("Unable to create a temporary file."), i18n("Apply failed"));
        return;
    }
    tempFile.write(lines.join(QLatin1Char('\n')).toUtf8());
    tempFile.write("\n");
    tempFile.flush();
    const QString tempPath = tempFile.fileName();
    tempFile.close();

    auto *process = new QProcess(this);
    m_runningProcess = process;

    connect(process, &QProcess::finished, this, [this, process, tempPath](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        QFile::remove(tempPath);
        process->deleteLater();
        m_runningProcess = nullptr;

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_sleepStatusLabel->setText(i18n("Sleep settings saved. Reboot for the changes to take full effect."));
            KMessageBox::information(this,
                                     i18n("Sleep settings have been saved. Reboot for the changes to take full effect."),
                                     i18n("Apply completed"));
        } else {
            KMessageBox::error(this,
                               i18n("Saving sleep settings failed.\n\n%1", output.right(4000)),
                               i18n("Apply failed"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, tempPath](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QFile::remove(tempPath);
            process->deleteLater();
            m_runningProcess = nullptr;
            KMessageBox::error(this,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Apply failed"));
        }
    });

    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-apply-sleep"),
                    tempPath});
}

void MainWindow::fixSleepProblems()
{
    if (m_runningProcess) {
        return;
    }

    if (m_sleepFixButton) {
        m_sleepFixButton->setEnabled(false);
        m_sleepFixButton->setText(i18n("Fixing…"));
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;

    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        process->deleteLater();
        m_runningProcess = nullptr;

        if (m_sleepFixButton) {
            m_sleepFixButton->setText(i18n("Fix detected problems"));
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            refreshSleepChecks();
            KMessageBox::information(this,
                                     i18n("The detected problems have been fixed. Reboot for the changes to take full effect."),
                                     i18n("Fix completed"));
        } else {
            refreshSleepChecks();
            KMessageBox::error(this,
                               i18n("Failed to fix the detected problems.\n\n%1", output.right(4000)),
                               i18n("Fix failed"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            if (m_sleepFixButton) {
                m_sleepFixButton->setText(i18n("Fix detected problems"));
            }
            KMessageBox::error(this,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Fix failed"));
        }
    });

    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-fix-sleep")});
}

QWidget *MainWindow::buildAboutTab()
{
    auto *page = new QWidget;
    auto *outerLayout = new QVBoxLayout(page);
    outerLayout->setContentsMargins(40, 40, 40, 40);
    outerLayout->setSpacing(20);

    auto *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(30);

    // Icon area (left column)
    auto *iconFrame = new QFrame(page);
    iconFrame->setFixedSize(96, 96);
    iconFrame->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #54b9c9; border-radius: 12px; }"));
    auto *iconLabel = new QLabel(iconFrame);
    iconLabel->setFixedSize(96, 96);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("miryu-toolkit")).pixmap(48, 48));
    contentLayout->addWidget(iconFrame, 0, Qt::AlignTop);

    // Text area (right column)
    auto *textLayout = new QVBoxLayout;
    textLayout->setSpacing(6);

    auto *titleLabel = new QLabel(i18n("Miryu Toolkit"), page);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 12);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    textLayout->addWidget(titleLabel);

    auto *copyright1 = new QLabel(QStringLiteral("© 2027 KairikiFedora"), page);
    textLayout->addWidget(copyright1);

    auto *copyright2 = new QLabel(QStringLiteral("© 2027 MiryuGaming"), page);
    textLayout->addWidget(copyright2);

    textLayout->addSpacing(12);

    auto *linksHeader = new QLabel(i18n("Project links"), page);
    QFont headerFont = linksHeader->font();
    headerFont.setBold(true);
    linksHeader->setFont(headerFont);
    textLayout->addWidget(linksHeader);

    // Three clickable links
    const QStringList linkUrls = {
        QStringLiteral("https://github.com/evernightvista/miryu-toolkit"),
        QStringLiteral("https://github.com/EvernightFedora"),
        QStringLiteral("https://github.com/miryugaming"),
    };
    const QStringList linkLabels = {
        i18n("Miryu Toolkit (Project Repository)"),
        QStringLiteral("KairikiFedora"),
        QStringLiteral("MiryuGaming"),
    };

    for (int i = 0; i < linkUrls.size(); ++i) {
        auto *link = new QLabel(
            QStringLiteral("<a href=\"%1\">%2</a>").arg(linkUrls[i], linkLabels[i]), page);
        link->setOpenExternalLinks(true);
        link->setTextInteractionFlags(Qt::TextBrowserInteraction);
        link->setCursor(Qt::PointingHandCursor);
        textLayout->addWidget(link);
    }

    contentLayout->addLayout(textLayout, 1);
    outerLayout->addLayout(contentLayout);
    outerLayout->addStretch();
    return page;
}

void MainWindow::refreshComponentStates()
{
    for (const ExtraComponent &component : std::as_const(m_components)) {
        refreshOneComponent(component);
    }
}

void MainWindow::refreshOneComponent(const ExtraComponent &component)
{
    int installedCount = 0;
    for (const QString &packageName : component.packages) {
        if (packageInstalled(packageName)) {
            ++installedCount;
        }
    }

    ComponentWidgets widgets = m_componentWidgets.value(component.id);
    if (!widgets.statusLabel || !widgets.installButton || !widgets.uninstallButton) {
        return;
    }

    const bool allInstalled = installedCount == component.packages.size();
    const bool anyInstalled = installedCount > 0;

    if (allInstalled) {
        widgets.statusLabel->setText(i18n("Status: installed"));
    } else if (anyInstalled) {
        widgets.statusLabel->setText(i18n("Status: partially installed (%1/%2)", installedCount, component.packages.size()));
    } else {
        widgets.statusLabel->setText(i18n("Status: not installed"));
    }

    widgets.installButton->setEnabled(!allInstalled && m_runningProcess == nullptr);
    widgets.uninstallButton->setEnabled(anyInstalled && m_runningProcess == nullptr);
}

bool MainWindow::packageInstalled(const QString &packageName) const
{
    QProcess rpm;
    rpm.start(QStringLiteral("rpm"), {QStringLiteral("-q"), packageName});
    if (!rpm.waitForFinished(2000)) {
        rpm.kill();
        rpm.waitForFinished();
        return false;
    }
    return rpm.exitStatus() == QProcess::NormalExit && rpm.exitCode() == 0;
}

void MainWindow::runPackageOperation(const ExtraComponent &component, const QString &operation)
{
    if (m_runningProcess) {
        return;
    }

    const QString actionText = operation == QStringLiteral("install") ? i18n("Install") : i18n("Uninstall");
    const QString question = i18n("Do you want to %1 “%2”?\n\nThe package helper will run: dnf5 %3 -y %4",
                                  actionText,
                                  component.name,
                                  operation,
                                  component.packages.join(QStringLiteral(" ")));

    if (KMessageBox::questionTwoActions(this,
                                        question,
                                        i18n("Confirm %1", actionText),
                                        KGuiItem(actionText),
                                        KStandardGuiItem::cancel())
        != KMessageBox::PrimaryAction) {
        return;
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    QStringList args;
    const QString helperName = (operation == QStringLiteral("install"))
        ? component.helperExecutable
        : component.removeHelperExecutable;
    const QString helperPath = QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QLatin1Char('/') + helperName;
    args << helperPath << operation;

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    logDialog->setWindowTitle(i18n("DNF5 details: %1", component.name));
    logDialog->resize(760, 460);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Detailed dnf5 installation log:"), logDialog);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Starting privileged package helper..."));
    logLayout->addWidget(logView, 1);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        QString text = QString::fromLocal8Bit(data).trimmed();
        text.replace(QStringLiteral("__NO_SEGFAULT_FOUND__"),
                    i18n("No segfault entries found in the kernel log (dmesg)."));
        logView->appendPlainText(text);
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QDialog> dialogGuard(logDialog);
    connect(process, &QProcess::finished, this, [this, process, component, actionText, viewGuard, dialogGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(output.trimmed());
        }
        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("%1 completed.", actionText));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("%1 completed.", actionText),
                                     i18n("Completed"));
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("%1 “%2” failed.", actionText, component.name));
            }
            KMessageBox::error(dialogGuard,
                               i18n("%1 “%2” failed. See the log window for details.", actionText, component.name),
                               i18n("Failed"));
        }
        dialogGuard->close();
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [this, process, actionText, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
                errViewGuard->appendPlainText(i18n("%1 failed", actionText));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
}

// ── Boot Menu (GRUB2) tab ──

QWidget *MainWindow::buildBootMenuTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    layout->addWidget(buildBootMenuSection());

    layout->addStretch();
    return page;
}

QWidget *MainWindow::buildBootMenuSection()
{
    auto *group = new QGroupBox(i18n("Boot Menu"));
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(12);

    // ── Boot menu options ──
    m_grubShowMenuCheck = new QCheckBox(i18n("Show boot menu"), group);
    m_grubShowMenuCheck->setToolTip(i18n("Show the GRUB menu before starting the default system."));
    groupLayout->addWidget(m_grubShowMenuCheck);

    auto *delayLayout = new QHBoxLayout();
    delayLayout->setSpacing(12);
    auto *delayLabel = new QLabel(i18n("Delay before booting"), group);
    m_grubDelaySpin = new QSpinBox(group);
    m_grubDelaySpin->setRange(-1, 3600);
    m_grubDelaySpin->setSuffix(i18n(" seconds"));
    m_grubDelaySpin->setSpecialValueText(i18n("wait indefinitely"));
    m_grubDelaySpin->setToolTip(i18n("Use -1 to wait until a menu entry is selected."));
    delayLayout->addWidget(delayLabel);
    delayLayout->addSpacing(8);
    delayLayout->addWidget(m_grubDelaySpin);
    delayLayout->addStretch();
    groupLayout->addLayout(delayLayout);

    m_grubRememberCheck = new QCheckBox(i18n("Remember last selected entry as the default"), group);
    groupLayout->addWidget(m_grubRememberCheck);

    groupLayout->addSpacing(4);

    // ── Kernel parameters ──
    auto *paramsTitle = new QLabel(i18n("Kernel Parameters"), group);
    QFont cardTitleFont = paramsTitle->font();
    cardTitleFont.setBold(true);
    paramsTitle->setFont(cardTitleFont);
    groupLayout->addWidget(paramsTitle);

    auto *currentLabel = new QLabel(i18n("Current kernel command line"), group);
    groupLayout->addWidget(currentLabel);

    m_grubCurrentParamsDisplay = new QTextEdit(group);
    m_grubCurrentParamsDisplay->setReadOnly(true);
    m_grubCurrentParamsDisplay->setMinimumHeight(76);
    m_grubCurrentParamsDisplay->setMaximumHeight(92);
    groupLayout->addWidget(m_grubCurrentParamsDisplay);

    auto *customLabel = new QLabel(i18n("Custom parameters written by this tool"), group);
    groupLayout->addWidget(customLabel);

    auto *listLayout = new QHBoxLayout();
    listLayout->setSpacing(12);

    m_grubParamListWidget = new QListWidget(group);
    m_grubParamListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grubParamListWidget->setAlternatingRowColors(true);
    m_grubParamListWidget->setMinimumHeight(132);
    connect(m_grubParamListWidget, &QListWidget::currentRowChanged, this, &MainWindow::refreshGrubActionButtons);
    listLayout->addWidget(m_grubParamListWidget, 1);

    auto *sideButtons = new QVBoxLayout();
    sideButtons->setSpacing(8);

    m_grubRemoveParamButton = new QPushButton(i18n("Remove"), group);
    m_grubEditParamButton = new QPushButton(i18n("Edit"), group);
    m_grubMoveUpButton = new QPushButton(i18n("Up"), group);
    m_grubMoveDownButton = new QPushButton(i18n("Down"), group);

    connect(m_grubRemoveParamButton, &QPushButton::clicked, this, &MainWindow::onRemoveGrubParam);
    connect(m_grubEditParamButton, &QPushButton::clicked, this, &MainWindow::onEditGrubParam);
    connect(m_grubMoveUpButton, &QPushButton::clicked, this, &MainWindow::onMoveGrubUp);
    connect(m_grubMoveDownButton, &QPushButton::clicked, this, &MainWindow::onMoveGrubDown);

    sideButtons->addWidget(m_grubRemoveParamButton);
    sideButtons->addWidget(m_grubEditParamButton);
    sideButtons->addWidget(m_grubMoveUpButton);
    sideButtons->addWidget(m_grubMoveDownButton);
    sideButtons->addStretch();
    listLayout->addLayout(sideButtons);
    groupLayout->addLayout(listLayout);

    auto *addLayout = new QHBoxLayout();
    addLayout->setSpacing(12);
    auto *newLabel = new QLabel(i18n("New parameter"), group);
    m_grubNewParamEdit = new QLineEdit(group);
    m_grubNewParamEdit->setPlaceholderText(i18n("Example: nomodeset"));
    m_grubAddParamButton = new QPushButton(i18n("Add"), group);
    m_grubAddParamButton->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    connect(m_grubNewParamEdit, &QLineEdit::returnPressed, this, &MainWindow::onAddGrubParam);
    connect(m_grubAddParamButton, &QPushButton::clicked, this, &MainWindow::onAddGrubParam);
    addLayout->addWidget(newLabel);
    addLayout->addSpacing(8);
    addLayout->addWidget(m_grubNewParamEdit, 1);
    addLayout->addSpacing(8);
    addLayout->addWidget(m_grubAddParamButton);
    groupLayout->addLayout(addLayout);

    groupLayout->addSpacing(4);

    // ── Bottom: status + save (no reboot button) ──
    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);

    m_grubStatusLabel = new QLabel(i18n("Saving requires administrator authentication through polkit."), group);
    m_grubStatusLabel->setWordWrap(true);

    m_grubSaveButton = new QPushButton(i18n("Save GRUB2 Settings"), group);
    m_grubSaveButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    connect(m_grubSaveButton, &QPushButton::clicked, this, &MainWindow::onSaveGrubClicked);

    bottomLayout->addWidget(m_grubStatusLabel, 1);
    bottomLayout->addWidget(m_grubSaveButton);
    groupLayout->addLayout(bottomLayout);

    return group;
}

void MainWindow::loadGrubConfig()
{
    if (!m_grubConfig || !m_grubConfig->load()) {
        if (m_grubStatusLabel) {
            m_grubStatusLabel->setText(i18n("Could not load the GRUB configuration."));
        }
        return;
    }
    updateGrubUiFromConfig();
}

void MainWindow::updateGrubUiFromConfig()
{
    if (!m_grubConfig) {
        return;
    }
    m_grubShowMenuCheck->setChecked(m_grubConfig->showMenu());
    m_grubDelaySpin->setValue(m_grubConfig->timeout());
    m_grubRememberCheck->setChecked(m_grubConfig->rememberLast());
    m_grubCurrentParamsDisplay->setPlainText(m_grubConfig->currentCmdline());
    setGrubParameterList(m_grubConfig->bootParams().split(QLatin1Char(' '), Qt::SkipEmptyParts));
    refreshGrubActionButtons();
}

QStringList MainWindow::grubParameterList() const
{
    QStringList result;
    for (int i = 0; i < m_grubParamListWidget->count(); ++i) {
        const auto *item = m_grubParamListWidget->item(i);
        if (item && !item->text().trimmed().isEmpty()) {
            result << item->text().trimmed();
        }
    }
    return result;
}

void MainWindow::setGrubParameterList(const QStringList &params)
{
    m_grubParamListWidget->clear();
    for (const QString &param : params) {
        const QString trimmed = param.trimmed();
        if (!trimmed.isEmpty()) {
            m_grubParamListWidget->addItem(trimmed);
        }
    }
}

bool MainWindow::validateGrubParameter(const QString &param, int ignoredRow)
{
    if (param.isEmpty()) {
        KMessageBox::information(this, i18n("Empty Parameter"), i18n("Enter one kernel parameter first."));
        return false;
    }

    if (param.contains(QRegularExpression(QStringLiteral(R"(\s)")))) {
        KMessageBox::error(this, i18n("Invalid Parameter"),
                           i18n("Add one parameter at a time. Spaces are not allowed."));
        return false;
    }

    const QRegularExpression safeParamRe(QStringLiteral(R"(^[A-Za-z0-9_./:=,+@%-]+$)"));
    if (!safeParamRe.match(param).hasMatch()) {
        KMessageBox::error(this, i18n("Invalid Parameter"),
                           i18n("Use only letters, numbers, and these characters: _ . / : = , + @ % -"));
        return false;
    }

    for (int i = 0; i < m_grubParamListWidget->count(); ++i) {
        if (i == ignoredRow) {
            continue;
        }
        const auto *item = m_grubParamListWidget->item(i);
        if (item && item->text() == param) {
            KMessageBox::information(this, i18n("Duplicate Parameter"),
                                     i18n("The parameter \xe2\x80\x9c%1\xe2\x80\x9d already exists.", param));
            return false;
        }
    }

    return true;
}

void MainWindow::onAddGrubParam()
{
    const QString param = m_grubNewParamEdit->text().trimmed();
    if (!validateGrubParameter(param)) {
        return;
    }

    m_grubParamListWidget->addItem(param);
    m_grubNewParamEdit->clear();
    m_grubNewParamEdit->setFocus();
    refreshGrubActionButtons();
}

void MainWindow::onRemoveGrubParam()
{
    const int row = m_grubParamListWidget->currentRow();
    if (row >= 0) {
        delete m_grubParamListWidget->takeItem(row);
    }
    refreshGrubActionButtons();
}

void MainWindow::onEditGrubParam()
{
    const int row = m_grubParamListWidget->currentRow();
    if (row < 0) {
        return;
    }

    auto *item = m_grubParamListWidget->item(row);
    bool ok = false;
    const QString edited = QInputDialog::getText(this, i18n("Edit Parameter"), i18n("Parameter"),
                                                 QLineEdit::Normal, item->text(), &ok).trimmed();
    if (ok && validateGrubParameter(edited, row)) {
        item->setText(edited);
    }
}

void MainWindow::onMoveGrubUp()
{
    const int row = m_grubParamListWidget->currentRow();
    if (row <= 0) {
        return;
    }

    auto *item = m_grubParamListWidget->takeItem(row);
    m_grubParamListWidget->insertItem(row - 1, item);
    m_grubParamListWidget->setCurrentRow(row - 1);
    refreshGrubActionButtons();
}

void MainWindow::onMoveGrubDown()
{
    const int row = m_grubParamListWidget->currentRow();
    if (row < 0 || row >= m_grubParamListWidget->count() - 1) {
        return;
    }

    auto *item = m_grubParamListWidget->takeItem(row);
    m_grubParamListWidget->insertItem(row + 1, item);
    m_grubParamListWidget->setCurrentRow(row + 1);
    refreshGrubActionButtons();
}

void MainWindow::refreshGrubActionButtons()
{
    if (!m_grubParamListWidget || !m_grubParamListWidget->isEnabled()) {
        m_grubRemoveParamButton->setEnabled(false);
        m_grubEditParamButton->setEnabled(false);
        m_grubMoveUpButton->setEnabled(false);
        m_grubMoveDownButton->setEnabled(false);
        return;
    }

    const int row = m_grubParamListWidget->currentRow();
    const bool hasSelection = row >= 0;
    m_grubRemoveParamButton->setEnabled(hasSelection);
    m_grubEditParamButton->setEnabled(hasSelection);
    m_grubMoveUpButton->setEnabled(row > 0);
    m_grubMoveDownButton->setEnabled(row >= 0 && row < m_grubParamListWidget->count() - 1);
}

void MainWindow::onSaveGrubClicked()
{
    if (!m_grubConfig) {
        return;
    }

    m_grubConfig->setShowMenu(m_grubShowMenuCheck->isChecked());
    m_grubConfig->setTimeout(m_grubDelaySpin->value());
    m_grubConfig->setRememberLast(m_grubRememberCheck->isChecked());
    m_grubConfig->setBootParams(grubParameterList().join(QLatin1Char(' ')));

    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/miryu-toolkit-grub-XXXXXX.cfg"));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        KMessageBox::error(this, i18n("Save Failed"), i18n("Could not create a temporary configuration file."));
        return;
    }

    const QString configContent = m_grubConfig->generateConfigContent();
    tempFile.write(configContent.toUtf8());
    tempFile.flush();
    const QString tempPath = tempFile.fileName();
    tempFile.close();

    const QString helperPath = resolveGrubHelperPath();
    if (!QFileInfo::exists(helperPath)) {
        QFile::remove(tempPath);
        KMessageBox::error(this, i18n("Helper Not Found"),
                           i18n("The privileged helper was not found at:\n%1\n\nInstall the project before saving GRUB2 settings.",
                                helperPath));
        return;
    }

    // ── Build a non-closeable log dialog ──
    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);   // no close button
    logDialog->setWindowTitle(i18n("Applying GRUB2 Settings"));
    logDialog->resize(640, 440);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Waiting for administrator authentication..."));
    logLayout->addWidget(logView, 1);

    // ── Async QProcess so the main window stays responsive ──
    auto *process = new QProcess(this);

    auto appendOutput = [logView](const QByteArray &data) {
        const QString text = QString::fromUtf8(data).trimmed();
        if (!text.isEmpty()) {
            logView->appendPlainText(text);
            logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, logDialog, [appendOutput, process]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [appendOutput, process]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QDialog> dialogGuard(logDialog);
    connect(process, &QProcess::finished, this, [this, process, dialogGuard, viewGuard, tempPath, appendOutput](int exitCode, QProcess::ExitStatus exitStatus) {
        // Flush any remaining output
        if (viewGuard) {
            appendOutput(process->readAllStandardOutput());
            appendOutput(process->readAllStandardError());
        }

        QFile::remove(tempPath);
        setGrubBusy(false);
        process->deleteLater();

        if (!dialogGuard) {
            return;
        }

        const bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);

        if (success) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("GRUB2 settings saved successfully."));
            }
            m_grubStatusLabel->setText(i18n("GRUB2 settings saved. Reboot to use the new boot menu."));
            KMessageBox::information(dialogGuard, i18n("Saved"),
                                     i18n("GRUB2 settings were saved and grub2-mkconfig completed successfully."));
            loadGrubConfig();
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("GRUB2 settings save failed."));
            }
            m_grubStatusLabel->setText(i18n("Save failed."));
            if (exitCode == -1) {
                // Process never started (pkexec not found, etc.)
                KMessageBox::error(dialogGuard, i18n("Save Failed"),
                                   i18n("Could not start the privileged helper. Make sure polkit is installed and run this program from a graphical session."));
            } else {
                KMessageBox::error(dialogGuard, i18n("Save Failed"),
                                   i18n("Could not save and regenerate GRUB2 configuration."));
            }
        }

        dialogGuard->close();
    });

    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [errViewGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
        }
        // Other errors (e.g. Crashed) are handled in finished()
    });

    setGrubBusy(true);
    m_grubStatusLabel->setText(i18n("Waiting for administrator authentication..."));

    process->start(QStringLiteral("pkexec"), QStringList() << helperPath << QStringLiteral("--apply") << tempPath);

    logDialog->show();
}

void MainWindow::setGrubBusy(bool busy)
{
    m_grubSaveButton->setEnabled(!busy);
    m_grubShowMenuCheck->setEnabled(!busy);
    m_grubDelaySpin->setEnabled(!busy);
    m_grubRememberCheck->setEnabled(!busy);
    m_grubParamListWidget->setEnabled(!busy);
    m_grubNewParamEdit->setEnabled(!busy);
    m_grubAddParamButton->setEnabled(!busy);
    refreshGrubActionButtons();
}

QString MainWindow::resolveGrubHelperPath() const
{
    const QString installedPath = QStringLiteral(GRUB_HELPER_PATH);
    if (QFileInfo::exists(installedPath)) {
        return installedPath;
    }

    const QString localPath = QCoreApplication::applicationDirPath()
                              + QLatin1Char('/')
                              + QStringLiteral("miryu-toolkit-grub-config-helper");
    if (QFileInfo::exists(localPath)) {
        return localPath;
    }

    return installedPath;
}

void MainWindow::setComponentsBusy(bool busy)
{
    for (const ExtraComponent &component : std::as_const(m_components)) {
        ComponentWidgets widgets = m_componentWidgets.value(component.id);
        if (widgets.installButton) {
            widgets.installButton->setEnabled(!busy);
        }
        if (widgets.uninstallButton) {
            widgets.uninstallButton->setEnabled(!busy);
        }
    }
}

void MainWindow::loadEnvironmentVariables()
{
    if (!m_environmentList) {
        return;
    }

    m_environmentList->clear();

    QFile file(QStringLiteral("/etc/environment"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_environmentStatus->setText(i18n("Unable to read /etc/environment. You can add variables and apply them later."));
        return;
    }

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        m_environmentList->addItem(line);
    }

    m_environmentStatus->setText(i18n("/etc/environment has been loaded. Log out and log back in for changes to take effect."));
}

void MainWindow::addEnvironmentVariable()
{
    bool ok = false;
    const QString line = QInputDialog::getText(this,
                                               i18n("Add environment variable"),
                                               i18n("Variable (NAME=VALUE):"),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok)
                             .trimmed();
    if (!ok || line.isEmpty()) {
        return;
    }

    QString error;
    if (!validateEnvironmentLine(line, &error)) {
        KMessageBox::error(this, error, i18n("Invalid format"));
        return;
    }

    m_environmentList->addItem(line);
}

void MainWindow::editEnvironmentVariable()
{
    auto *item = m_environmentList->currentItem();
    if (!item) {
        KMessageBox::information(this, i18n("Select an environment variable first."), i18n("No item selected"));
        return;
    }

    bool ok = false;
    const QString line = QInputDialog::getText(this,
                                               i18n("Edit environment variable"),
                                               i18n("Variable (NAME=VALUE):"),
                                               QLineEdit::Normal,
                                               item->text(),
                                               &ok)
                             .trimmed();
    if (!ok || line.isEmpty()) {
        return;
    }

    QString error;
    if (!validateEnvironmentLine(line, &error)) {
        KMessageBox::error(this, error, i18n("Invalid format"));
        return;
    }

    item->setText(line);
}

void MainWindow::removeEnvironmentVariable()
{
    delete m_environmentList->takeItem(m_environmentList->currentRow());
}

void MainWindow::applyEnvironmentVariables()
{
    if (m_runningProcess) {
        return;
    }

    QStringList lines;
    for (int i = 0; i < m_environmentList->count(); ++i) {
        const QString line = m_environmentList->item(i)->text().trimmed();
        QString error;
        if (!validateEnvironmentLine(line, &error)) {
            KMessageBox::error(this, error, i18n("Invalid format"));
            return;
        }
        lines << line;
    }

    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/miryu-environment-XXXXXX"));
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        KMessageBox::error(this, i18n("Unable to create a temporary file."), i18n("Apply failed"));
        return;
    }

    const QString tempPath = tempFile.fileName();
    QTextStream stream(&tempFile);
    for (const QString &line : lines) {
        stream << line << Qt::endl;
    }
    tempFile.close();

    auto *process = new QProcess(this);
    m_runningProcess = process;

    connect(process, &QProcess::finished, this, [this, process, tempPath](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        QFile::remove(tempPath);
        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            m_environmentStatus->setText(i18n("Saved to /etc/environment. Log out and log back in for changes to take effect."));
            KMessageBox::information(this, i18n("System-wide environment variables have been saved. Log out and log back in for changes to take effect."), i18n("Apply completed"));
        } else {
            KMessageBox::error(this,
                               i18n("Writing /etc/environment failed.\n\n%1", output.right(4000)),
                               i18n("Apply failed"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, tempPath](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QFile::remove(tempPath);
            process->deleteLater();
            m_runningProcess = nullptr;
            KMessageBox::error(this,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Apply failed"));
        }
    });

    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-apply-environment"),
                    tempPath});
}

void MainWindow::restoreDefaultEnvironmentVariables()
{
    if (m_runningProcess) {
        return;
    }

    if (KMessageBox::questionTwoActions(this,
                                        i18n("Restore the default system-wide environment variables?"),
                                        i18n("Restore defaults"),
                                        KGuiItem(i18n("Restore defaults")),
                                        KStandardGuiItem::cancel())
        != KMessageBox::PrimaryAction) {
        return;
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;

    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        process->deleteLater();
        m_runningProcess = nullptr;

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            setDefaultEnvironmentVariables();
            m_environmentStatus->setText(i18n("Default system-wide environment variables have been restored. Log out and log back in for changes to take effect."));
            KMessageBox::information(this, i18n("Default system-wide environment variables have been restored. Log out and log back in for changes to take effect."), i18n("Defaults restored"));
        } else {
            KMessageBox::error(this,
                               i18n("Restoring default system-wide environment variables failed.\n\n%1", output.right(4000)),
                               i18n("Restore failed"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            KMessageBox::error(this,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Restore failed"));
        }
    });

    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-restore-environment")});
}

void MainWindow::setDefaultEnvironmentVariables()
{
    if (!m_environmentList) {
        return;
    }

    m_environmentList->clear();
    m_environmentList->addItems({
        QStringLiteral("GTK_IM_MODULE=fcitx"),
        QStringLiteral("QT_IM_MODULE=fcitx"),
        QStringLiteral("QT_IM_MODULES=\"wayland;fcitx\""),
        QStringLiteral("XMODIFIERS=@im=fcitx"),
        QStringLiteral("SDL_IM_MODULE=fcitx"),
    });
}

bool MainWindow::validateEnvironmentLine(const QString &line, QString *errorText) const
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*=.*$"));

    if (!pattern.match(line).hasMatch()) {
        if (errorText) {
            *errorText = i18n("Environment variables must use the NAME=VALUE format. NAME may only contain letters, digits and underscores, and must not start with a digit.");
        }
        return false;
    }

    if (line.contains(QLatin1Char('\n')) || line.contains(QLatin1Char('\r'))) {
        if (errorText) {
            *errorText = i18n("A single environment variable must not contain line breaks.");
        }
        return false;
    }

    return true;
}

QString MainWindow::currentUserName()
{
    // Try SUDO_USER first (set by pkexec), then USER, then logname
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString user = env.value(QStringLiteral("SUDO_USER"));
    if (user.isEmpty()) {
        user = env.value(QStringLiteral("USER"));
    }
    if (user.isEmpty()) {
        QProcess p;
        p.start(QStringLiteral("logname"));
        p.waitForFinished(3000);
        user = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    }
    return user;
}

void MainWindow::cleanupOldKernel()
{
    if (m_runningProcess) {
        return;
    }

    if (KMessageBox::questionTwoActions(this,
                                        i18n("Remove previous kernel versions? This will run: dnf-3 remove --oldinstallonly"),
                                        i18n("Clean up previous kernel"),
                                        KGuiItem(i18n("Clean up")),
                                        KStandardGuiItem::cancel())
        != KMessageBox::PrimaryAction) {
        return;
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Cleaning up previous kernel..."));
    }

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    logDialog->setWindowTitle(i18n("Kernel cleanup log"));
    logDialog->resize(760, 460);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Removing previous kernel versions via dnf-3..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Starting privileged kernel cleanup helper..."));
    logLayout->addWidget(logView, 1);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        QString text = QString::fromLocal8Bit(data).trimmed();
        text.replace(QStringLiteral("__NO_SEGFAULT_FOUND__"),
                    i18n("No segfault entries found in the kernel log (dmesg)."));
        logView->appendPlainText(text);
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QDialog> dialogGuard(logDialog);
    connect(process, &QProcess::finished, this, [this, process, viewGuard, dialogGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        // Detect "nothing to do" or "done" as completed
        bool completed = false;
        QString lowerOutput = output.toLower();
        if (exitStatus == QProcess::NormalExit &&
            (lowerOutput.contains(QStringLiteral("nothing to do")) ||
             lowerOutput.contains(QStringLiteral("done")) ||
             exitCode == 0)) {
            completed = true;
        }

        if (completed) {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Kernel cleanup completed."));
            }
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Kernel cleanup completed."));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("Previous kernel versions have been cleaned up."),
                                     i18n("Cleanup completed"));
        } else {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Kernel cleanup failed."));
            }
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Kernel cleanup failed."));
            }
            KMessageBox::error(dialogGuard,
                               i18n("Failed to clean up previous kernel versions. See the log window for details."),
                               i18n("Cleanup failed"));
        }
        dialogGuard->close();
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [this, process, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Kernel cleanup failed."));
            }
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Cleanup failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-cleanup-kernel");

    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
}

void MainWindow::collectSystemLogs()
{
    if (m_runningProcess) {
        return;
    }

    const QString user = currentUserName();

    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Collecting system logs..."));
    }

    // Progress dialog
    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    logDialog->setWindowTitle(i18n("Collecting system logs"));
    logDialog->resize(640, 420);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Collecting system information and logs. This may take a moment..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Starting privileged log collection helper..."));
    logLayout->addWidget(logView, 1);

    // Accumulate stdout separately so the archive path can be extracted
    // reliably in the finished handler. The readyReadStandardOutput signal
    // consumes stdout data as it arrives, so readAllStandardOutput() in
    // the finished callback would otherwise return an empty buffer.
    auto stdoutAccum = std::make_shared<QByteArray>();

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput, stdoutAccum]() {
        QByteArray data = process->readAllStandardOutput();
        stdoutAccum->append(data);
        appendOutput(data);
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QDialog> dialogGuard(logDialog);
    connect(process, &QProcess::finished, this, [this, process, viewGuard, dialogGuard, stdoutAccum](int exitCode, QProcess::ExitStatus exitStatus) {
        // Drain any remaining output
        stdoutAccum->append(process->readAllStandardOutput());
        const QByteArray stderrRemainder = process->readAllStandardError();
        if (!stderrRemainder.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(QString::fromLocal8Bit(stderrRemainder).trimmed());
        }

        // The helper prints the archive path on the last line of stdout
        QString archivePath = QString::fromLocal8Bit(*stdoutAccum).trimmed().section(QLatin1Char('\n'), -1).trimmed();

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System logs collected: %1", archivePath));
            }
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Collection completed. Archive saved to: %1", archivePath));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("System logs have been collected and saved to:\n%1", archivePath),
                                     i18n("Collection completed"));
        } else {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Log collection failed."));
            }
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Log collection failed."));
            }
            KMessageBox::error(dialogGuard,
                               i18n("Failed to collect system logs. See the log window for details."),
                               i18n("Collection failed"));
        }
        dialogGuard->close();
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [this, process, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Log collection failed."));
            }
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Collection failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-collect-logs");
    if (!user.isEmpty()) {
        args << user;
    }

    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
}

void MainWindow::updateSystem()
{
    if (m_runningProcess) {
        return;
    }

    // Immediately invoke the privileged helper via pkexec. This triggers
    // polkit authentication (title: "更新操作系统需要认证") right away.
    // The helper runs "dnf5 check-update --refresh" as root, which exits with:
    //   100 – updates are available
    //   0   – system is already up to date
    //   1   – error
    //   127 – polkit authentication was canceled or failed
    auto *checkProcess = new QProcess(this);
    m_runningProcess = checkProcess;
    setComponentsBusy(true);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Checking for available updates..."));
    }

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    logDialog->setWindowTitle(i18n("System update"));
    logDialog->resize(760, 480);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Checking for available updates with dnf5. This may take a moment..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Running: dnf5 check-update --refresh"));
    logLayout->addWidget(logView, 1);

    auto *cancelButton = new QPushButton(i18n("Cancel update"), logDialog);
    cancelButton->setObjectName(QStringLiteral("cancelUpdateButton"));
    cancelButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-cancel")));
    cancelButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logLayout->addWidget(cancelButton, 0, Qt::AlignRight);

    logDialog->setProperty("cancelled", false);

    connect(cancelButton, &QPushButton::clicked, this, [this, logView, cancelButton, logDialog]() {
        if (m_runningProcess) {
            kill(m_runningProcess->processId(), SIGINT);
            logView->appendPlainText(i18n("Cancelling..."));
            cancelButton->setEnabled(false);
            logDialog->setProperty("cancelled", true);
        }
    });

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(checkProcess, &QProcess::readyReadStandardOutput, logDialog, [checkProcess, appendOutput]() {
        appendOutput(checkProcess->readAllStandardOutput());
    });
    connect(checkProcess, &QProcess::readyReadStandardError, logDialog, [checkProcess, appendOutput]() {
        appendOutput(checkProcess->readAllStandardError());
    });

    QPointer<QDialog> dialogGuard(logDialog);
    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QLabel> hintGuard(logHint);
    connect(checkProcess, &QProcess::finished, this, [this, checkProcess, dialogGuard, viewGuard, hintGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray stdoutRemainder = checkProcess->readAllStandardOutput();
        if (!stdoutRemainder.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(QString::fromLocal8Bit(stdoutRemainder).trimmed());
        }
        const QByteArray stderrRemainder = checkProcess->readAllStandardError();
        if (!stderrRemainder.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(QString::fromLocal8Bit(stderrRemainder).trimmed());
        }

        checkProcess->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        const bool wasCancelled = dialogGuard->property("cancelled").toBool();
        if (wasCancelled) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Update cancelled."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update cancelled."));
            }
            dialogGuard->close();
            return;
        }

        const bool updatesAvailable = (exitStatus == QProcess::NormalExit && exitCode == 100);
        const bool noUpdates = (exitStatus == QProcess::NormalExit && exitCode == 0);
        const bool authCanceled = (exitStatus == QProcess::NormalExit && exitCode == 127);

        if (updatesAvailable) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Available updates detected."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Updates are available."));
            }
            if (KMessageBox::questionTwoActions(dialogGuard,
                                                i18n("dnf5 detected available updates. Do you want to update the system now?"),
                                                i18n("Update system"),
                                                KGuiItem(i18n("Update")),
                                                KStandardGuiItem::cancel())
                == KMessageBox::PrimaryAction) {
                if (hintGuard) {
                    hintGuard->setText(i18n("Updating the system with dnf5. Please wait..."));
                }
                startPrivilegedSystemUpdate(dialogGuard, viewGuard);
            } else {
                if (viewGuard) {
                    viewGuard->appendPlainText(i18n("Update cancelled."));
                }
                if (m_bottomStatus) {
                    m_bottomStatus->setText(i18n("Update cancelled."));
                }
                dialogGuard->close();
            }
        } else if (noUpdates) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Your system is already up to date."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Your system is already up to date."));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("Your system is already up to date."),
                                     i18n("No updates"));
            dialogGuard->close();
        } else if (authCanceled) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Authentication was canceled."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Authentication was canceled."));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("Authentication was canceled. No updates were checked or installed."),
                                     i18n("Authentication canceled"));
            dialogGuard->close();
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Failed to check for updates."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update check failed."));
            }
            KMessageBox::error(dialogGuard,
                               i18n("Failed to check for available updates. See the log window for details."),
                               i18n("Check failed"));
            dialogGuard->close();
        }
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(checkProcess, &QProcess::errorOccurred, this, [this, checkProcess, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            checkProcess->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update check failed."));
            }
            if (errDialogGuard) {
                auto *cancelBtn = errDialogGuard->findChild<QPushButton*>(QStringLiteral("cancelUpdateButton"));
                if (cancelBtn) {
                    cancelBtn->setEnabled(false);
                }
            }
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Check failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    QStringList checkArgs;
    checkArgs << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-update-system")
               << QStringLiteral("check");
    checkProcess->start(QStringLiteral("pkexec"), checkArgs);
    logDialog->show();
}

void MainWindow::startPrivilegedSystemUpdate(QDialog *logDialog, QPlainTextEdit *logView)
{
    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    // Re-enable the cancel button for the update phase
    auto *cancelButton = logDialog->findChild<QPushButton*>(QStringLiteral("cancelUpdateButton"));
    if (cancelButton) {
        cancelButton->setEnabled(true);
    }
    logDialog->setProperty("cancelled", false);

    logView->appendPlainText(i18n("Starting privileged system update helper..."));
    logView->appendPlainText(i18n("Running: dnf5 update --refresh"));

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        QString text = QString::fromLocal8Bit(data).trimmed();
        text.replace(QStringLiteral("__NO_SEGFAULT_FOUND__"),
                    i18n("No segfault entries found in the kernel log (dmesg)."));
        logView->appendPlainText(text);
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QDialog> dialogGuard(logDialog);
    QPointer<QPlainTextEdit> viewGuard(logView);
    connect(process, &QProcess::finished, this, [this, process, dialogGuard, viewGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        // Disable the cancel button — process is no longer running
        auto *cancelBtn = dialogGuard->findChild<QPushButton*>(QStringLiteral("cancelUpdateButton"));
        if (cancelBtn) {
            cancelBtn->setEnabled(false);
        }

        const bool wasCancelled = dialogGuard->property("cancelled").toBool();
        if (wasCancelled) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Update cancelled."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update cancelled."));
            }
        } else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("System update completed."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update completed."));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("The system has been updated successfully."),
                                     i18n("Update completed"));
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("System update failed."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update failed."));
            }
            KMessageBox::error(dialogGuard,
                               i18n("Failed to update the system. See the log window for details."),
                               i18n("Update failed"));
        }
        dialogGuard->close();
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [this, process, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update failed."));
            }
            if (errDialogGuard) {
                auto *cancelBtn = errDialogGuard->findChild<QPushButton*>(QStringLiteral("cancelUpdateButton"));
                if (cancelBtn) {
                    cancelBtn->setEnabled(false);
                }
            }
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Update failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-update-system")
         << QStringLiteral("update");
    process->start(QStringLiteral("pkexec"), args);
}

void MainWindow::listFailedServices()
{
    auto *process = new QProcess(this);

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    logDialog->setWindowTitle(i18n("Failed systemd service units"));
    logDialog->resize(760, 460);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Listing failed systemd service units..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Running: systemctl --failed --no-pager"));
    logLayout->addWidget(logView, 1);

    auto *closeButton = new QPushButton(i18n("Close"), logDialog);
    closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logLayout->addWidget(closeButton, 0, Qt::AlignRight);
    connect(closeButton, &QPushButton::clicked, logDialog, &QDialog::close);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    connect(process, &QProcess::finished, this, [viewGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Command completed."));
            }
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Command failed (exit code %1).", exitCode));
            }
        }
    });

    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [errViewGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start systemctl."));
            }
        }
    });

    process->start(QStringLiteral("systemctl"), {QStringLiteral("--failed"), QStringLiteral("--no-pager")});
    logDialog->show();
}

void MainWindow::unlockRpmDatabase()
{
    if (m_runningProcess) {
        return;
    }

    if (KMessageBox::questionTwoActions(this,
                                        i18n("Before unlocking the RPM database, please check whether dnf5 and dnf5daemon-server have completed their transactions.\n\nAre you sure you want to unlock the RPM database?"),
                                        i18n("Unlock RPM database"),
                                        KGuiItem(i18n("Unlock")),
                                        KStandardGuiItem::cancel())
        != KMessageBox::PrimaryAction) {
        return;
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Unlocking RPM database..."));
    }

    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("RPM database has been unlocked."));
            }
            KMessageBox::information(this,
                                     i18n("RPM database has been unlocked."),
                                     i18n("Unlock completed"));
        } else {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Failed to unlock RPM database."));
            }
            KMessageBox::error(this,
                               i18n("Failed to unlock the RPM database."),
                               i18n("Unlock failed"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Failed to unlock RPM database."));
            }
            KMessageBox::error(this,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Unlock failed"));
        }
    });

    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-unlock-rpm");
    process->start(QStringLiteral("pkexec"), args);
}

void MainWindow::viewDnf5Log()
{
    const QString logPath = QStringLiteral("/var/log/dnf5.log");

    if (!QFile::exists(logPath)) {
        KMessageBox::information(this,
                                 i18n("The dnf5 log file does not exist at %1.", logPath),
                                 i18n("File not found"));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(logPath));
}

void MainWindow::cleanupUnusedPackages()
{
    if (m_runningProcess) {
        return;
    }

    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Cleaning unused packages..."));
    }

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint);
    logDialog->setWindowTitle(i18n("Clean unused packages"));
    logDialog->resize(760, 460);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Removing unused dependency packages with dnf5 autoremove..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Starting privileged dnf5 autoremove helper..."));
    logView->appendPlainText(i18n("Running: dnf5 autoremove --assumeyes"));
    logLayout->addWidget(logView, 1);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    QPointer<QDialog> dialogGuard(logDialog);
    connect(process, &QProcess::finished, this, [this, process, viewGuard, dialogGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty() && viewGuard) {
            viewGuard->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (!dialogGuard) {
            return;
        }

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Cleanup completed."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup completed."));
            }
            KMessageBox::information(dialogGuard,
                                     i18n("Unused packages have been cleaned up."),
                                     i18n("Cleanup completed"));
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Cleanup failed."));
            }
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup failed."));
            }
            KMessageBox::error(dialogGuard,
                               i18n("Failed to clean up unused packages. See the log window for details."),
                               i18n("Cleanup failed"));
        }
        dialogGuard->close();
    });

    QPointer<QDialog> errDialogGuard(logDialog);
    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [this, process, errViewGuard, errDialogGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup failed."));
            }
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
            KMessageBox::error(errDialogGuard,
                               i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."),
                               i18n("Cleanup failed"));
            if (errDialogGuard) {
                errDialogGuard->close();
            }
        }
    });

    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-autoremove");
    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
}

void MainWindow::viewCrashInfo()
{
    auto *process = new QProcess(this);

    auto *logDialog = new QDialog(this);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    logDialog->setWindowTitle(i18n("Software crash information"));
    logDialog->resize(760, 460);

    auto *logLayout = new QVBoxLayout(logDialog);
    auto *logHint = new QLabel(i18n("Showing software crash information (dmesg | grep segfault)..."), logDialog);
    logHint->setWordWrap(true);
    logLayout->addWidget(logHint);

    auto *logView = new QPlainTextEdit(logDialog);
    logView->setReadOnly(true);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    logView->appendPlainText(i18n("Running: dmesg 2>&1 | grep -i segfault"));
    logLayout->addWidget(logView, 1);

    auto *closeButton = new QPushButton(i18n("Close"), logDialog);
    closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    closeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    logLayout->addWidget(closeButton, 0, Qt::AlignRight);
    connect(closeButton, &QPushButton::clicked, logDialog, &QDialog::close);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        QString text = QString::fromLocal8Bit(data).trimmed();
        text.replace(QStringLiteral("__NO_SEGFAULT_FOUND__"),
                    i18n("No segfault entries found in the kernel log (dmesg)."));
        logView->appendPlainText(text);
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, logDialog, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    QPointer<QPlainTextEdit> viewGuard(logView);
    connect(process, &QProcess::finished, this, [viewGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Command completed."));
            }
        } else {
            if (viewGuard) {
                viewGuard->appendPlainText(i18n("Command completed (exit code %1).", exitCode));
            }
        }
    });

    QPointer<QPlainTextEdit> errViewGuard(logView);
    connect(process, &QProcess::errorOccurred, this, [errViewGuard](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (errViewGuard) {
                errViewGuard->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            }
        }
    });

    // dmesg requires root to read the kernel ring buffer on systems where
    // kernel.dmesg_restrict=1 (the default on Fedora and many distros), so
    // running "dmesg | grep segfault" as a normal GUI user shows nothing.
    // Run the crash-info helper as root via pkexec so the segfault entries
    // are visible, exactly like "sudo dmesg | grep segfault" in a terminal.
    QStringList args;
    args << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-view-crash");
    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
}

