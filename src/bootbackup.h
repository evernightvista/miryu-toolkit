#pragma once

#include "bootdata.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct BackupInfo {
    QString id;
    QString created;
    QString phase;
    QString targetRoot;
    QString rootDevice;
    QString directory;
    QString digest;
    QJsonArray operations;
    QJsonArray files;

    QJsonObject json() const;
};

class BootBackup {
public:
    static QString defaultRoot()
    {
        return QStringLiteral("/var/lib/miryu-toolkit/boot-backups");
    }

    static QStringList configurationPaths(const QString &sourceRoot);
    static QJsonArray list(const QString &root = defaultRoot(), int limit = 50);
    static QList<BackupInfo> infos(const QString &root = defaultRoot(), int limit = 50);
    static QStringList restorePaths(const QString &root, const QString &id, QString *error);

    static bool validateRestore(const QString &root, const QString &id,
                                const QString &digest, const RepairTarget &target,
                                const QString &sourceRoot, QString *error);
    static bool validateRestore(const QString &root, const QString &id,
                                const QString &digest, QString *error);

    static bool restore(const QString &root, const QString &id, const QString &digest,
                        const RepairTarget &target, const QString &sourceRoot, QString *error);
    static bool restore(const QString &root, const QString &id,
                        const RepairTarget &target, const QString &sourceRoot, QString *error);
    static bool restore(const QString &root, const QString &id, QString *error);

    // sourceRoot is where files are read now; target preserves the stable identity
    // used to select the same Linux system during a later restore.
    bool create(const BootSnapshot &snapshot, const RepairTarget &target,
                const QString &sourceRoot, const QStringList &paths,
                const QString &backupRoot, QString *error);
    bool create(const BootSnapshot &snapshot, const QString &targetRoot,
                const QString &rootDevice, const QStringList &paths,
                const QString &backupRoot, QString *error);

    bool record(const QString &action, bool success, const QString &details, QString *error);
    bool record(const QString &action, bool success, QString *error);
    bool finish(bool success, QString *error);

    QString directory() const { return directory_; }
    QString id() const { return id_; }
    QString digest() const;

private:
    bool save(QString *error);
    bool captureCreatedFiles(QString *error);

    QString directory_;
    QString id_;
    QString sourceRoot_;
    RepairTarget target_;
    QJsonObject manifest_;
};
