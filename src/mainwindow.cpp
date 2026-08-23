#include "mainwindow.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
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
#include <QScrollBar>
#include <QStandardPaths>
#include <QThread>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

#ifndef TOOLKIT_LIBEXEC_DIR
#define TOOLKIT_LIBEXEC_DIR "/usr/libexec/miryu-toolkit"
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupComponents();
    setupTransparencyLevels();

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
    m_tabs->addTab(buildSystemAssistantTab(), i18n("Miryu System Assistant"));
    m_tabs->addTab(buildEnvironmentTab(), i18n("System-wide environment variables"));
    m_tabs->addTab(buildExtrasTab(), i18n("Install additional components"));
    m_tabs->addTab(buildPersonalizationTab(), i18n("Personalization"));
    m_tabs->addTab(buildAboutTab(), i18n("About"));
    rootLayout->addWidget(m_tabs, 1);

    auto *bottomLayout = new QHBoxLayout;
    m_bottomStatus = new QLabel(i18n("Ready"), central);
    bottomLayout->addWidget(m_bottomStatus);
    bottomLayout->addStretch();
    rootLayout->addLayout(bottomLayout);

    setCentralWidget(central);
    resize(980, 620);
    setWindowTitle(i18n("Miryu Toolkit"));

    refreshComponentStates();
    loadEnvironmentVariables();
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [this, process, component, actionText, logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty()) {
            logView->appendPlainText(output.trimmed());
        }
        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            logView->appendPlainText(i18n("%1 completed.", actionText));
        } else {
            logView->appendPlainText(i18n("%1 “%2” failed.", actionText, component.name));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, actionText, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            logView->appendPlainText(i18n("%1 failed", actionText));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
        }
    });

    process->start(QStringLiteral("pkexec"), args);
    logDialog->show();
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [this, process, logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty()) {
            logView->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

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
            logView->appendPlainText(i18n("Kernel cleanup completed."));
            KMessageBox::information(this,
                                     i18n("Previous kernel versions have been cleaned up."),
                                     i18n("Cleanup completed"));
        } else {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Kernel cleanup failed."));
            }
            logView->appendPlainText(i18n("Kernel cleanup failed."));
            KMessageBox::error(this,
                               i18n("Failed to clean up previous kernel versions. See the log window for details."),
                               i18n("Cleanup failed"));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Kernel cleanup failed."));
            }
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput, stdoutAccum]() {
        QByteArray data = process->readAllStandardOutput();
        stdoutAccum->append(data);
        appendOutput(data);
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [this, process, logView, closeButtons, stdoutAccum](int exitCode, QProcess::ExitStatus exitStatus) {
        // Drain any remaining output
        stdoutAccum->append(process->readAllStandardOutput());
        const QByteArray stderrRemainder = process->readAllStandardError();
        if (!stderrRemainder.trimmed().isEmpty()) {
            logView->appendPlainText(QString::fromLocal8Bit(stderrRemainder).trimmed());
        }

        // The helper prints the archive path on the last line of stdout
        QString archivePath = QString::fromLocal8Bit(*stdoutAccum).trimmed().section(QLatin1Char('\n'), -1).trimmed();

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System logs collected: %1", archivePath));
            }
            logView->appendPlainText(i18n("Collection completed. Archive saved to: %1", archivePath));
            KMessageBox::information(this,
                                     i18n("System logs have been collected and saved to:\n%1", archivePath),
                                     i18n("Collection completed"));
        } else {
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Log collection failed."));
            }
            logView->appendPlainText(i18n("Log collection failed."));
            KMessageBox::error(this,
                               i18n("Failed to collect system logs. See the log window for details."),
                               i18n("Collection failed"));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Log collection failed."));
            }
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(checkProcess, &QProcess::readyReadStandardOutput, this, [checkProcess, appendOutput]() {
        appendOutput(checkProcess->readAllStandardOutput());
    });
    connect(checkProcess, &QProcess::readyReadStandardError, this, [checkProcess, appendOutput]() {
        appendOutput(checkProcess->readAllStandardError());
    });

    connect(checkProcess, &QProcess::finished, this, [this, checkProcess, logDialog, logView, logHint, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        const QByteArray stdoutRemainder = checkProcess->readAllStandardOutput();
        if (!stdoutRemainder.trimmed().isEmpty()) {
            logView->appendPlainText(QString::fromLocal8Bit(stdoutRemainder).trimmed());
        }
        const QByteArray stderrRemainder = checkProcess->readAllStandardError();
        if (!stderrRemainder.trimmed().isEmpty()) {
            logView->appendPlainText(QString::fromLocal8Bit(stderrRemainder).trimmed());
        }

        checkProcess->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        const bool updatesAvailable = (exitStatus == QProcess::NormalExit && exitCode == 100);
        const bool noUpdates = (exitStatus == QProcess::NormalExit && exitCode == 0);
        const bool authCanceled = (exitStatus == QProcess::NormalExit && exitCode == 127);

        if (updatesAvailable) {
            logView->appendPlainText(i18n("Available updates detected."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Updates are available."));
            }
            if (KMessageBox::questionTwoActions(logDialog,
                                                i18n("dnf5 detected available updates. Do you want to update the system now?"),
                                                i18n("Update system"),
                                                KGuiItem(i18n("Update")),
                                                KStandardGuiItem::cancel())
                == KMessageBox::PrimaryAction) {
                logHint->setText(i18n("Updating the system with dnf5. Please wait..."));
                startPrivilegedSystemUpdate(logDialog, logView, closeButtons);
            } else {
                logView->appendPlainText(i18n("Update cancelled."));
                if (m_bottomStatus) {
                    m_bottomStatus->setText(i18n("Update cancelled."));
                }
                closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
            }
        } else if (noUpdates) {
            logView->appendPlainText(i18n("Your system is already up to date."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Your system is already up to date."));
            }
            KMessageBox::information(logDialog,
                                     i18n("Your system is already up to date."),
                                     i18n("No updates"));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
        } else if (authCanceled) {
            logView->appendPlainText(i18n("Authentication was canceled."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Authentication was canceled."));
            }
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
        } else {
            logView->appendPlainText(i18n("Failed to check for updates."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update check failed."));
            }
            KMessageBox::error(logDialog,
                               i18n("Failed to check for available updates. See the log window for details."),
                               i18n("Check failed"));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
        }
    });

    connect(checkProcess, &QProcess::errorOccurred, this, [this, checkProcess, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            checkProcess->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Update check failed."));
            }
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
        }
    });

    QStringList checkArgs;
    checkArgs << QStringLiteral(TOOLKIT_LIBEXEC_DIR) + QStringLiteral("/miryu-toolkit-update-system")
               << QStringLiteral("check");
    checkProcess->start(QStringLiteral("pkexec"), checkArgs);
    logDialog->show();
}

void MainWindow::startPrivilegedSystemUpdate(QDialog *logDialog, QPlainTextEdit *logView,
                                             QDialogButtonBox *closeButtons)
{
    auto *process = new QProcess(this);
    m_runningProcess = process;
    setComponentsBusy(true);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [this, process, logDialog, logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty()) {
            logView->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            logView->appendPlainText(i18n("System update completed."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update completed."));
            }
            KMessageBox::information(logDialog,
                                     i18n("The system has been updated successfully."),
                                     i18n("Update completed"));
        } else {
            logView->appendPlainText(i18n("System update failed."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update failed."));
            }
            KMessageBox::error(logDialog,
                               i18n("Failed to update the system. See the log window for details."),
                               i18n("Update failed"));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("System update failed."));
            }
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            logView->appendPlainText(i18n("Command completed."));
        } else {
            logView->appendPlainText(i18n("Command failed (exit code %1).", exitCode));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            logView->appendPlainText(i18n("Unable to start systemctl."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

    auto appendOutput = [logView](const QByteArray &data) {
        if (data.isEmpty()) {
            return;
        }
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
        logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
    };

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [this, process, logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput())
            + QString::fromLocal8Bit(process->readAllStandardError());
        if (!output.trimmed().isEmpty()) {
            logView->appendPlainText(output.trimmed());
        }

        process->deleteLater();
        m_runningProcess = nullptr;
        refreshComponentStates();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            logView->appendPlainText(i18n("Cleanup completed."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup completed."));
            }
            KMessageBox::information(this,
                                     i18n("Unused packages have been cleaned up."),
                                     i18n("Cleanup completed"));
        } else {
            logView->appendPlainText(i18n("Cleanup failed."));
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup failed."));
            }
            KMessageBox::error(this,
                               i18n("Failed to clean up unused packages. See the log window for details."),
                               i18n("Cleanup failed"));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_runningProcess = nullptr;
            refreshComponentStates();
            if (m_bottomStatus) {
                m_bottomStatus->setText(i18n("Unused packages cleanup failed."));
            }
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, logDialog);
    closeButtons->button(QDialogButtonBox::Close)->setEnabled(false);
    connect(closeButtons, &QDialogButtonBox::rejected, logDialog, &QDialog::close);
    logLayout->addWidget(closeButtons);

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

    connect(process, &QProcess::readyReadStandardOutput, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, appendOutput]() {
        appendOutput(process->readAllStandardError());
    });

    connect(process, &QProcess::finished, this, [logView, closeButtons](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            logView->appendPlainText(i18n("Command completed."));
        } else {
            logView->appendPlainText(i18n("Command completed (exit code %1).", exitCode));
        }
        closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
    });

    connect(process, &QProcess::errorOccurred, this, [logView, closeButtons](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            logView->appendPlainText(i18n("Unable to start pkexec. Make sure polkit is installed and run this program from a graphical session."));
            closeButtons->button(QDialogButtonBox::Close)->setEnabled(true);
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

// --- SelectableImageCard ---

SelectableImageCard::SelectableImageCard(const QString &imagePath, const QString &title,
                                         const QString &description, QWidget *parent)
    : QFrame(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(this);
    QPixmap pixmap(imagePath);
    if (!pixmap.isNull()) {
        m_imageLabel->setPixmap(pixmap.scaled(240, 140, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMinimumSize(240, 140);
    layout->addWidget(m_imageLabel);

    m_titleLabel = new QLabel(title, this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 1);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    setToolTip(description);
    setFixedSize(270, 200);
    setCursor(Qt::PointingHandCursor);
    updateStyle();
}

void SelectableImageCard::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    updateStyle();
}

void SelectableImageCard::updateStyle()
{
    if (m_selected) {
        setStyleSheet(QStringLiteral(
            "SelectableImageCard { border: 3px solid #3daee9; border-radius: 8px; }"));
    } else {
        setStyleSheet(QStringLiteral(
            "SelectableImageCard { border: 1px solid #c0c0c0; border-radius: 8px; }"));
    }
}

void SelectableImageCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        Q_EMIT clicked();
    }
    QFrame::mousePressEvent(event);
}

void SelectableImageCard::enterEvent(QEnterEvent *event)
{
    QFrame::enterEvent(event);
    Q_EMIT hovered();
}

void SelectableImageCard::leaveEvent(QEvent *event)
{
    QFrame::leaveEvent(event);
    Q_EMIT unhovered();
}

// --- Personalization tab ---

void MainWindow::setupTransparencyLevels()
{
    m_transparencyLevels = {
        {
            QStringLiteral("tongtou"),
            i18n("Real"),
            i18n("Glass-like texture, clear and delicate"),
            3,
            14,
            QStringLiteral(":/personalization/data/images/tongtou.png"),
        },
        {
            QStringLiteral("default"),
            i18n("Default"),
            i18n("Retains transparency, comfortable balance"),
            7,
            14,
            QStringLiteral(":/personalization/data/images/default.png"),
        },
        {
            QStringLiteral("mosha"),
            i18n("Soft"),
            i18n("Blur texture, clear and easy to read"),
            13,
            14,
            QStringLiteral(":/personalization/data/images/mosha.png"),
        },
    };
}

QWidget *MainWindow::buildPersonalizationTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *group = new QGroupBox(i18n("Interface transparency"), page);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setSpacing(16);

    auto *hint = new QLabel(i18n("Choose an interface transparency level. After clicking Apply, it is recommended to log out and log back in for the changes to take effect."), group);
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);

    auto *cardsLayout = new QHBoxLayout;
    cardsLayout->setSpacing(20);
    cardsLayout->setAlignment(Qt::AlignCenter);

    for (int i = 0; i < m_transparencyLevels.size(); ++i) {
        const TransparencyLevel &level = m_transparencyLevels[i];
        auto *card = new SelectableImageCard(level.imageResource, level.name, level.description, group);

        const int index = i;
        connect(card, &SelectableImageCard::clicked, this, [this, index]() {
            selectTransparencyCard(index);
        });
        connect(card, &SelectableImageCard::hovered, this, [this, index]() {
            if (m_transparencyDescription) {
                m_transparencyDescription->setText(m_transparencyLevels[index].description);
            }
        });
        connect(card, &SelectableImageCard::unhovered, this, [this]() {
            if (m_transparencyDescription && m_selectedTransparencyIndex >= 0
                && m_selectedTransparencyIndex < m_transparencyLevels.size()) {
                m_transparencyDescription->setText(m_transparencyLevels[m_selectedTransparencyIndex].description);
            }
        });

        m_transparencyCards.append(card);
        cardsLayout->addWidget(card);
    }

    groupLayout->addLayout(cardsLayout);

    m_transparencyDescription = new QLabel(group);
    m_transparencyDescription->setAlignment(Qt::AlignCenter);
    m_transparencyDescription->setWordWrap(true);
    m_transparencyDescription->setMinimumHeight(36);
    QFont descFont = m_transparencyDescription->font();
    descFont.setPointSize(descFont.pointSize() - 1);
    m_transparencyDescription->setFont(descFont);
    groupLayout->addWidget(m_transparencyDescription);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    auto *applyButton = new QPushButton(i18n("Apply"), group);
    applyButton->setMinimumWidth(120);
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::applyTransparencyLevel);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addStretch();
    groupLayout->addLayout(buttonLayout);

    layout->addWidget(group);
    layout->addStretch();

    m_selectedTransparencyIndex = detectCurrentTransparencyLevel();
    selectTransparencyCard(m_selectedTransparencyIndex);

    return page;
}

void MainWindow::selectTransparencyCard(int index)
{
    if (index < 0 || index >= m_transparencyLevels.size()) {
        return;
    }

    m_selectedTransparencyIndex = index;

    for (int i = 0; i < m_transparencyCards.size(); ++i) {
        if (m_transparencyCards[i]) {
            m_transparencyCards[i]->setSelected(i == index);
        }
    }

    if (m_transparencyDescription) {
        m_transparencyDescription->setText(m_transparencyLevels[index].description);
    }
}

int MainWindow::detectCurrentTransparencyLevel() const
{
    const QString kwinrcPath = QDir::homePath() + QStringLiteral("/.config/kwinrc");
    QFile file(kwinrcPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 1;
    }

    bool inBlurSection = false;
    int blurStrength = -1;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            inBlurSection = (line.compare(QStringLiteral("[Effect-blur]"), Qt::CaseInsensitive) == 0);
            continue;
        }
        if (inBlurSection && line.startsWith(QStringLiteral("BlurStrength="), Qt::CaseInsensitive)) {
            blurStrength = line.section(QLatin1Char('='), 1).trimmed().toInt();
        }
    }
    file.close();

    for (int i = 0; i < m_transparencyLevels.size(); ++i) {
        if (m_transparencyLevels[i].blurStrength == blurStrength) {
            return i;
        }
    }
    return 1;
}

void MainWindow::applyTransparencyLevel()
{
    if (m_selectedTransparencyIndex < 0 || m_selectedTransparencyIndex >= m_transparencyLevels.size()) {
        return;
    }

    const TransparencyLevel &level = m_transparencyLevels[m_selectedTransparencyIndex];

    // Write only BlurStrength through kwriteconfig6 (which uses KConfig
    // internally). kwriteconfig6 writes to disk and calls KConfig::sync(),
    // but the D-Bus ConfigChanged notification it emits may not be properly
    // received by KWin in time. We manually emit the signal below to ensure
    // KWin's in-memory KConfig cache is invalidated before we reload blur.
    const QString blurVal = QString::number(level.blurStrength);

    QProcess kwriteConfig;
    kwriteConfig.start(QStringLiteral("kwriteconfig6"),
                       {QStringLiteral("--file"), QStringLiteral("kwinrc"),
                        QStringLiteral("--group"), QStringLiteral("Effect-blur"),
                        QStringLiteral("--key"), QStringLiteral("BlurStrength"),
                        blurVal});
    kwriteConfig.waitForFinished(5000);

    // Manually emit the KConfig D-Bus ConfigChanged signal so that KWin
    // immediately invalidates its in-memory KConfig cache for the
    // [Effect-blur] group. Without this, kwriteconfig6's own notification
    // may not reach KWin in time, causing the blur effect to read stale
    // cached values when reloaded (requiring a second Apply click).
    QDBusMessage configChangedMsg = QDBusMessage::createSignal(
        QStringLiteral("/kwinrc"),
        QStringLiteral("org.kde.kconfig.notify"),
        QStringLiteral("ConfigChanged"));
    configChangedMsg << QStringList{QStringLiteral("Effect-blur")};
    QDBusConnection::sessionBus().send(configChangedMsg);

    // Process pending D-Bus events so the ConfigChanged signal is actually
    // dispatched on the wire before we proceed.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    // Unload and reload the blur effect to attempt immediate application.
    // The two commands must run sequentially: first unload, wait for it to
    // finish, then load.
    const QString dbusBin = QStandardPaths::findExecutable(QStringLiteral("qdbus-qt6"));
    const QString dbusToUse = dbusBin.isEmpty() ? QStringLiteral("qdbus") : dbusBin;

    // Step 1: unload the blur effect
    QProcess unloadProcess;
    unloadProcess.start(dbusToUse, {
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects.unloadEffect"), QStringLiteral("blur")
    });
    unloadProcess.waitForFinished(5000);

    // Small delay between unload and load to ensure the effect is fully
    // unloaded before reloading.
    QThread::msleep(500);

    // Step 2: load the blur effect back
    QProcess loadProcess;
    loadProcess.start(dbusToUse, {
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects.loadEffect"), QStringLiteral("blur")
    });
    loadProcess.waitForFinished(5000);

    if (m_bottomStatus) {
        m_bottomStatus->setText(i18n("Transparency level applied. It is recommended to log out and log back in for the changes to take effect."));
    }
    KMessageBox::information(this,
                             i18n("Interface transparency level has been applied. It is recommended to log out and log back in for the changes to take effect."),
                             i18n("Apply completed"));
}
