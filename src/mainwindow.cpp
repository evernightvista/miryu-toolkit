#include "mainwindow.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QAbstractItemView>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QVBoxLayout>

#include <memory>

#ifndef TOOLKIT_LIBEXEC_DIR
#define TOOLKIT_LIBEXEC_DIR "/usr/libexec/miryu-toolkit"
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
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
    m_tabs->addTab(buildExtrasTab(), i18n("Miscellaneous"));
    m_tabs->addTab(buildEnvironmentTab(), i18n("System-wide environment variables"));
    m_tabs->addTab(buildAboutTab(), i18n("About"));
    rootLayout->addWidget(m_tabs, 1);

    auto *bottomLayout = new QHBoxLayout;
    m_bottomStatus = new QLabel(i18n("Ready"), central);
    bottomLayout->addWidget(m_bottomStatus);
    bottomLayout->addStretch();

    auto *cleanupKernelButton = new QPushButton(i18n("Clean up previous kernel"), central);
    connect(cleanupKernelButton, &QPushButton::clicked, this, &MainWindow::cleanupOldKernel);
    bottomLayout->addWidget(cleanupKernelButton);

    auto *collectLogsButton = new QPushButton(i18n("Collect system logs"), central);
    connect(collectLogsButton, &QPushButton::clicked, this, &MainWindow::collectSystemLogs);
    bottomLayout->addWidget(collectLogsButton);

    auto *refreshButton = new QPushButton(i18n("Refresh status"), central);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshComponentStates);
    bottomLayout->addWidget(refreshButton);
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
        "QFrame { background-color: #793faf; border-radius: 12px; }"));
    auto *iconLabel = new QLabel(iconFrame);
    iconLabel->setFixedSize(96, 96);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("deepin-repair-tools")).pixmap(48, 48));
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
        logView->appendPlainText(QString::fromLocal8Bit(data).trimmed());
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
