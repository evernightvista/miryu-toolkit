#pragma once

#include "bootbackup.h"
#include "bootdata.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class BootScanWorker;
class QProcess;
class QThread;

Q_DECLARE_METATYPE(BootSnapshot)
Q_DECLARE_METATYPE(QList<BackupInfo>)

class BootService : public QObject
{
    Q_OBJECT

public:
    explicit BootService(QObject *parent = nullptr);
    ~BootService() override;

    const BootSnapshot &snapshot() const { return m_snapshot; }
    const QList<BackupInfo> &backups() const { return m_backups; }
    bool isBusy() const { return m_busy; }

#ifdef MIRYU_BOOT_PAGES_TEST
    void setSnapshotForTest(const BootSnapshot &snapshot) { m_snapshot = snapshot; }
#endif

public Q_SLOTS:
    void refresh();
    bool apply(const QString &action, const QJsonObject &request,
               const QString &description);

Q_SIGNALS:
    void snapshotReady(const BootSnapshot &snapshot);
    void backupsReady(const QList<BackupInfo> &backups);
    void busyChanged(bool busy);
    void output(const QString &text);
    void operationFinished(bool success, const QString &message,
                           const QString &action);
    void scanProgress(const QString &stage);

private:
    void beginScan();
    void finishScan(const BootSnapshot &snapshot,
                    const QList<BackupInfo> &backups);
    void setBusy(bool busy);
    QString helperPath() const;
    void finishOperation(bool success, const QString &message);
    void drainProcessOutput();
    bool rejectOperation(const QString &action, const QString &message);

    BootSnapshot m_snapshot;
    QList<BackupInfo> m_backups;
    QThread *m_scanThread = nullptr;
    BootScanWorker *m_worker = nullptr;
    QProcess *m_process = nullptr;
    QString m_action;
    QString m_description;
    QString m_errorOutput;
    bool m_busy = false;
    bool m_scanning = false;
    bool m_refreshPending = false;
    bool m_shuttingDown = false;
};
