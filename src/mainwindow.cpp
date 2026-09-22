#include "mainwindow.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
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
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <signal.h>
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

    auto *subtitle = new QLabel(i18n("Manage Miryu components, maintenance tasks and system-wide environment variables in one place."), central);
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
    loadGrubConfig();
}

void MainWindow::setupComponents()
{
    m_components = {
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
        "QFrame { background-color: #92e796; border-radius: 12px; }"));
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

