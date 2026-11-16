#include "bootservice.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QThread>

#ifndef BOOT_HELPER_PATH
#define BOOT_HELPER_PATH "/usr/libexec/miryu-toolkit/miryu-toolkit-boot-helper"
#endif

class BootScanWorker : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void scan()
    {
        const BootSnapshot result = BootProbe::inspect(QStringLiteral("/"),
            [this](const QString &stage) { Q_EMIT progress(stage); });
        const QList<BackupInfo> backupResult = BootBackup::infos();
        Q_EMIT scanReady(result, backupResult);
    }

Q_SIGNALS:
    void progress(const QString &stage);
    void scanReady(const BootSnapshot &snapshot,
                   const QList<BackupInfo> &backups);
};

BootService::BootService(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BootSnapshot>("BootSnapshot");
    qRegisterMetaType<QList<BackupInfo>>("QList<BackupInfo>");

    m_scanThread = new QThread(this);
    m_worker = new BootScanWorker;
    m_worker->moveToThread(m_scanThread);
    connect(m_scanThread, &QThread::finished,
            m_worker, &QObject::deleteLater);
    connect(m_worker, &BootScanWorker::progress,
            this, &BootService::scanProgress);
    connect(m_worker, &BootScanWorker::scanReady,
            this, [this](const BootSnapshot &snapshot,
                         const QList<BackupInfo> &backups) {
                finishScan(snapshot, backups);
            });
    m_scanThread->start();
}

BootService::~BootService()
{
    m_shuttingDown = true;
    m_refreshPending = false;

    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        BootProbe::stop(*m_process);
        m_process->closeWriteChannel();
        delete m_process;
        m_process = nullptr;
    }

    if (m_scanThread) {
        m_scanThread->quit();
        m_scanThread->wait();
    }
}

void BootService::refresh()
{
    if (m_shuttingDown)
        return;
    if (m_busy) {
        m_refreshPending = true;
        return;
    }
    beginScan();
}

void BootService::beginScan()
{
    if (m_busy || m_scanning || m_shuttingDown)
        return;

    m_refreshPending = false;
    m_scanning = true;
    setBusy(true);
    Q_EMIT scanProgress(i18n("Scanning boot configuration"));
    QMetaObject::invokeMethod(m_worker, &BootScanWorker::scan,
                              Qt::QueuedConnection);
}

void BootService::finishScan(const BootSnapshot &snapshot,
                             const QList<BackupInfo> &backups)
{
    if (m_shuttingDown)
        return;

    m_snapshot = snapshot;
    m_backups = backups;
    m_scanning = false;
    Q_EMIT snapshotReady(m_snapshot);
    Q_EMIT backupsReady(m_backups);
    setBusy(false);

    if (m_refreshPending)
        beginScan();
}

void BootService::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged(m_busy);
}

QString BootService::helperPath() const
{
    const QString installed = QStringLiteral(BOOT_HELPER_PATH);
    const QFileInfo installedInfo(installed);
    if (installedInfo.isFile() && installedInfo.isExecutable())
        return installed;

    const QString local = QCoreApplication::applicationDirPath()
        + QStringLiteral("/miryu-toolkit-boot-helper");
    const QFileInfo localInfo(local);
    if (localInfo.isFile() && localInfo.isExecutable())
        return local;
    return installed;
}

bool BootService::rejectOperation(const QString &action, const QString &message)
{
    Q_EMIT output(message);
    Q_EMIT operationFinished(false, message, action);
    return false;
}

bool BootService::apply(const QString &action, const QJsonObject &request,
                        const QString &description)
{
    static const QSet<QString> allowedActions{
        QStringLiteral("efi-order"), QStringLiteral("efi-next"),
        QStringLiteral("efi-toggle"), QStringLiteral("efi-rename"),
        QStringLiteral("efi-remove"), QStringLiteral("efi-add"),
        QStringLiteral("efi-batch"),
        QStringLiteral("mok-import"), QStringLiteral("mok-revoke"),
        QStringLiteral("repair"), QStringLiteral("restore")
    };

    if (!allowedActions.contains(action))
        return rejectOperation(action, i18n("The requested boot operation is not supported."));
    if (m_busy || m_process || m_shuttingDown)
        return rejectOperation(action, i18n("Another boot scan or operation is already in progress."));

    const QString helper = helperPath();
    const QFileInfo helperInfo(helper);
    if (!helperInfo.isFile() || !helperInfo.isExecutable()) {
        return rejectOperation(action,
            i18n("The privileged boot helper was not found or is not executable: %1", helper));
    }

    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty())
        return rejectOperation(action, i18n("pkexec is required for privileged boot operations."));

    m_action = action;
    m_description = description.isEmpty()
        ? i18n("Privileged boot operation") : description;
    m_errorOutput.clear();
    setBusy(true);
    Q_EMIT output(m_description);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    connect(m_process, &QProcess::started, this, [this, payload]() {
        if (!m_process || m_shuttingDown)
            return;
        disconnect(m_process, &QProcess::started, this, nullptr);
        if (m_process->write(payload) != payload.size()) {
            Q_EMIT output(i18n("Could not send the operation request to the privileged helper."));
            BootProbe::stop(*m_process);
            return;
        }
        m_process->closeWriteChannel();
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromUtf8(m_process->readAllStandardOutput());
        if (!text.isEmpty())
            Q_EMIT output(text);
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process)
            return;
        const QString text = QString::fromUtf8(m_process->readAllStandardError());
        if (!text.isEmpty()) {
            m_errorOutput.append(text);
            Q_EMIT output(text);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || !m_process)
                    return;
                const QString message = i18n(
                    "Could not start administrator authorization or the boot helper: %1",
                    m_process->errorString());
                finishOperation(false, message);
            });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (!m_process)
                    return;
                drainProcessOutput();
                const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
                QString message;
                if (success) {
                    message = i18n("%1 completed successfully.", m_description);
                } else {
                    const QString reason = m_errorOutput.trimmed();
                    if (exitStatus == QProcess::CrashExit) {
                        message = i18n("%1 stopped abnormally.", m_description);
                    } else {
                        message = i18n("%1 failed with exit status %2.",
                                       m_description, exitCode);
                    }
                    if (!reason.isEmpty())
                        message += QLatin1Char('\n') + reason;
                }
                finishOperation(success, message);
            });

    m_process->start(pkexec,
        {helper, QStringLiteral("--apply-stdin"), action,
         QStringLiteral("--lang"), QLocale().name()});
    return true;
}

void BootService::drainProcessOutput()
{
    if (!m_process)
        return;
    const QString standardOutput = QString::fromUtf8(m_process->readAllStandardOutput());
    const QString standardError = QString::fromUtf8(m_process->readAllStandardError());
    if (!standardOutput.isEmpty())
        Q_EMIT output(standardOutput);
    if (!standardError.isEmpty()) {
        m_errorOutput.append(standardError);
        Q_EMIT output(standardError);
    }
}

void BootService::finishOperation(bool success, const QString &message)
{
    if (!m_process || m_shuttingDown)
        return;

    const QString action = m_action;
    if (m_process->state() != QProcess::NotRunning)
        BootProbe::stop(*m_process);
    m_process->deleteLater();
    m_process = nullptr;
    m_action.clear();
    m_description.clear();
    m_errorOutput.clear();
    m_refreshPending = true;
    setBusy(false);
    Q_EMIT output(message);
    // Start the rescan before the completion dialog runs its nested event loop so
    // the pages already show the new firmware state when the dialog is dismissed.
    if (m_refreshPending)
        beginScan();
    Q_EMIT operationFinished(success, message, action);
}

#include "bootservice.moc"
