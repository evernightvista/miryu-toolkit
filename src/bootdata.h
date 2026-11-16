#pragma once

#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

enum class BootSecurityState {
    Unknown,
    Disabled,
    Enabled,
    Unsupported
};

using SecureBootState = BootSecurityState;

struct FirmwareEntry {
    QString number;
    QString label;
    QString path;
    QString partitionUuid;
    bool enabled = false;
    bool current = false;
    QString resolvedPath;

    QString identity() const
    {
        return number + QLatin1Char('|') + label + QLatin1Char('|') + path
            + QLatin1Char('|') + partitionUuid + QLatin1Char('|')
            + (enabled ? QLatin1Char('1') : QLatin1Char('0'));
    }
};

struct MokCertificate {
    QString subject;
    QString commonName;
    QString issuer;
    QString notAfter;
    QString fingerprint;
};

struct BootDevice {
    QString path;
    QString type;
    QString filesystem;
    QString size;
    QString partuuid;
    QString parttype;
    QString parent;
    QString partitionTable;
    QString description;

    QString label() const
    {
        return path + QStringLiteral(" · ") + size
            + (filesystem.isEmpty() ? QString() : QStringLiteral(" · ") + filesystem)
            + (description.isEmpty() ? QString() : QStringLiteral(" · ") + description);
    }
};

struct EspMount {
    QString path;
    QString source;
    QString partuuid;
};

struct RepairTarget {
    QString root;
    QString rootDevice;
    bool automatic = false;
    QString subvolume;
    QString label;
};

struct BootSnapshot {
    bool uefi = false;
    QString architecture;
    QString bootCurrent;
    QString bootNext;
    QStringList bootOrder;
    QList<FirmwareEntry> firmware;
    QString efiBootManagerOutput;

    BootSecurityState secureBoot = BootSecurityState::Unknown;
    BootSecurityState setupMode = BootSecurityState::Unknown;
    BootSecurityState moduleSigEnforce = BootSecurityState::Unknown;
    QString enrolledMok;
    QString pendingMok;
    QList<MokCertificate> enrolledCertificates;
    QList<MokCertificate> pendingCertificates;
    QStringList securityMessages;

    QList<EspMount> espMounts;
    QList<BootDevice> devices;
    QList<RepairTarget> repairTargets;
    QStringList messages;

    const FirmwareEntry *entry(const QString &number) const
    {
        for (const auto &item : firmware) {
            if (item.number == number)
                return &item;
        }
        return nullptr;
    }
};

namespace BootProbe {

struct CommandResult {
    bool completed = false;
    int exitCode = -1;
    QString output;
    QString error;
};

using Progress = std::function<void(const QString &)>;

BootSnapshot inspect(const QString &root = QStringLiteral("/"), const Progress &progress = {});
CommandResult readCommand(const QString &program, const QStringList &arguments = {},
                          int timeoutMs = 6500);
QString runReadOnly(const QString &program, const QStringList &arguments = {});
void stop(QProcess &process);

BootSnapshot parseEfiBootManager(const QString &output);
QList<MokCertificate> parseMokCertificates(const QString &output);

QList<EspMount> discoverEspMounts();
QList<BootDevice> discoverDevices();
QList<RepairTarget> mountedLinuxTargets();
QList<RepairTarget> automaticRepairTargets(const QList<BootDevice> &devices,
                                           const QList<RepairTarget> &mounted = {});

bool isOnEsp(const QString &path, const QList<EspMount> &mounts);
bool firmwareAddArguments(const QString &path, const QString &label,
                          const QList<EspMount> &mounts, QStringList *arguments,
                          QString *error);
bool firmwareAddArguments(const QString &path, const QString &label,
                          QStringList *arguments, QString *error);

bool validateDerCertificate(const QByteArray &certificate, QString *error = nullptr);
QString deviceFingerprint(const QList<BootDevice> &devices);
QString grubTarget(const QString &architecture);
QString grubBiosTarget(const QString &architecture);
bool validSubvolume(const QString &subvolume);
QStringList repairDiagnostics(const RepairTarget &target, const BootSnapshot &snapshot,
                              bool legacyBios = false);

// Direct read/write access to a Boot#### EFI variable. Some firmware ignores or
// misapplies efibootmgr's -b modifications, so the privileged helper uses this
// as a fallback and to verify that a change really reached the variable store.
struct BootVariable {
    quint32 variableAttributes = 0;
    quint32 loadOptionAttributes = 0;
    quint16 filePathLength = 0;
    QString description;
    QByteArray raw;
    int devicePathOffset = 0;
    bool active() const;
};

QString bootVariablePath(const QString &number);
BootSecurityState firmwareVariable(const QString &root, const QString &name);
bool readBootVariable(const QString &path, BootVariable *variable, QString *error = nullptr);
bool rewriteBootVariableDescription(const QString &path, const QString &description,
                                    QString *error = nullptr);
bool setBootVariableActive(const QString &path, bool active, QString *error = nullptr);

} // namespace BootProbe
