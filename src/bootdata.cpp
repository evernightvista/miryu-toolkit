#include "bootdata.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <KLocalizedString>

#include <algorithm>
#include <QtEndian>

namespace {

constexpr const char *espPartitionType = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

QString rooted(const QString &root, const QString &path)
{
    if (root == QLatin1String("/"))
        return QDir::cleanPath(QLatin1Char('/') + path);
    return QDir(root).filePath(path);
}

QString mountUnescape(const QString &value)
{
    QString result;
    result.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        if (value.at(i) == QLatin1Char('\\') && i + 3 < value.size()) {
            const QString octal = value.mid(i + 1, 3);
            bool ok = false;
            const int byte = octal.toInt(&ok, 8);
            if (ok && byte >= 0 && byte <= 255
                && QRegularExpression(QStringLiteral("^[0-7]{3}$")).match(octal).hasMatch()) {
                result.append(QChar(byte));
                i += 3;
                continue;
            }
        }
        result.append(value.at(i));
    }
    return result;
}

QString canonicalDirectory(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink() ? info.canonicalFilePath() : QString();
}

QString blockDeviceName(QString source)
{
    const int options = source.indexOf(QLatin1Char('['));
    if (options >= 0)
        source.truncate(options);
    return QDir::cleanPath(source);
}

BootSecurityState firmwareFlag(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return BootSecurityState::Unknown;
    const QByteArray value = file.read(6);
    if (value.size() < 5)
        return BootSecurityState::Unknown;
    if (value.at(4) == 0)
        return BootSecurityState::Disabled;
    if (value.at(4) == 1)
        return BootSecurityState::Enabled;
    return BootSecurityState::Unknown;
}

BootSecurityState moduleFlag(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return BootSecurityState::Unknown;
    const QByteArray value = file.read(32).trimmed().toLower();
    if (value == "1" || value == "y" || value == "yes")
        return BootSecurityState::Enabled;
    if (value == "0" || value == "n" || value == "no")
        return BootSecurityState::Disabled;
    return BootSecurityState::Unknown;
}

QString architecture()
{
    const QString machine = BootProbe::runReadOnly(QStringLiteral("uname"),
                                                   {QStringLiteral("-m")}).trimmed();
    if (machine == QLatin1String("x86_64"))
        return QStringLiteral("x64");
    if (machine == QLatin1String("aarch64") || machine == QLatin1String("arm64"))
        return QStringLiteral("aa64");
    if (QRegularExpression(QStringLiteral("^i[3-6]86$")).match(machine).hasMatch())
        return QStringLiteral("ia32");
    return machine;
}

QString readFindmnt(const QStringList &arguments)
{
    return BootProbe::runReadOnly(QStringLiteral("findmnt"), arguments).trimmed();
}

QString sourceAt(const QString &path)
{
    return blockDeviceName(readFindmnt({QStringLiteral("-rn"), QStringLiteral("-M"), path,
                                       QStringLiteral("-o"), QStringLiteral("SOURCE")}));
}

QString operatingSystemName(const QString &root)
{
    QFile file(rooted(root, QStringLiteral("etc/os-release")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(file.read(128 * 1024));
    const auto match = QRegularExpression(
        QStringLiteral(R"OS(^PRETTY_NAME=("([^"]*)"|'([^']*)'|(.*))$)OS"),
        QRegularExpression::MultilineOption).match(text);
    if (!match.hasMatch())
        return {};
    return match.captured(2).isEmpty()
        ? (match.captured(3).isEmpty() ? match.captured(4).trimmed() : match.captured(3))
        : match.captured(2);
}

bool containedFile(const QString &root, const QString &path)
{
    const QString base = QFileInfo(root).canonicalFilePath();
    const QString file = QFileInfo(path).canonicalFilePath();
    if (base.isEmpty() || file.isEmpty() || !QFileInfo(file).isFile())
        return false;
    return base == QLatin1String("/") || file.startsWith(base + QLatin1Char('/'));
}

bool linuxSystem(const QString &root)
{
    const QString clean = canonicalDirectory(root);
    return !clean.isEmpty()
        && containedFile(clean, rooted(clean, QStringLiteral("etc/os-release")))
        && containedFile(clean, rooted(clean, QStringLiteral("usr/bin/sh")));
}

QString jsonString(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isString() ? value.toString() : value.toVariant().toString();
}

void appendDevices(const QJsonArray &items, QList<BootDevice> *devices)
{
    for (const auto &value : items) {
        const QJsonObject object = value.toObject();
        BootDevice device;
        device.path = QDir::cleanPath(jsonString(object, "name"));
        device.type = jsonString(object, "type");
        device.filesystem = jsonString(object, "fstype");
        device.size = jsonString(object, "size");
        device.partuuid = jsonString(object, "partuuid");
        device.parttype = jsonString(object, "parttype").toLower();
        device.partitionTable = jsonString(object, "pttype").toLower();
        const QString parent = jsonString(object, "pkname").trimmed();
        if (!parent.isEmpty())
            device.parent = QDir::cleanPath(QLatin1Char('/') + parent);
        const QString model = jsonString(object, "model").trimmed();
        const QString label = jsonString(object, "label").trimmed();
        device.description = model + (!model.isEmpty() && !label.isEmpty() ? QStringLiteral(" · ") : QString()) + label;
        if (device.path.startsWith(QLatin1String("/dev/"))
            && QStringList{QStringLiteral("disk"), QStringLiteral("part"),
                           QStringLiteral("crypt"), QStringLiteral("lvm"),
                           QStringLiteral("mpath"), QStringLiteral("loop")}.contains(device.type)) {
            devices->append(device);
        }
        appendDevices(object.value(QStringLiteral("children")).toArray(), devices);
    }
}

QString executableIn(const QString &root, const QString &name)
{
    if (root == QLatin1String("/")) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            return found;
    }
    for (const auto &directory : {QStringLiteral("usr/sbin"), QStringLiteral("usr/bin"),
                                  QStringLiteral("sbin"), QStringLiteral("bin")}) {
        const QString candidate = rooted(root, directory + QLatin1Char('/') + name);
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable()
            && containedFile(root, candidate))
            return info.canonicalFilePath();
    }
    return {};
}

void resolveFirmware(BootSnapshot *snapshot)
{
    for (auto &entry : snapshot->firmware) {
        entry.resolvedPath.clear();
        QString relative = entry.path;
        relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!relative.startsWith(QLatin1Char('/')))
            relative.prepend(QLatin1Char('/'));
        if (QDir::cleanPath(relative) != relative
            || !relative.startsWith(QLatin1String("/EFI/"), Qt::CaseInsensitive)
            || !relative.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)) {
            continue;
        }
        for (const auto &mount : snapshot->espMounts) {
            if (mount.partuuid.isEmpty()
                || mount.partuuid.compare(entry.partitionUuid, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString base = QFileInfo(mount.path).canonicalFilePath();
            const QString efiBase = QFileInfo(base + QStringLiteral("/EFI")).canonicalFilePath();
            const QString candidate = base + relative;
            const QFileInfo info(candidate);
            const QString resolved = info.canonicalFilePath();
            if (base.isEmpty() || efiBase.isEmpty() || resolved.isEmpty()
                || !info.isFile() || !resolved.startsWith(efiBase + QLatin1Char('/'))) {
                continue;
            }
            entry.resolvedPath = resolved;
            break;
        }
    }
}

bool allowedReadCommand(const QString &program, const QStringList &arguments, QString *error)
{
    const QString name = QFileInfo(program).fileName();
    if (name == QLatin1String("chroot") || name == QLatin1String("systemd-nspawn")) {
        *error = i18nd("miryu-toolkit", "Container and %1 wrappers are not read-only probes.", QStringLiteral("chroot"));
        return false;
    }
    bool allowed = false;
    if (name == QLatin1String("efibootmgr"))
        allowed = arguments == QStringList{QStringLiteral("-v")};
    else if (name == QLatin1String("mokutil"))
        allowed = arguments == QStringList{QStringLiteral("--list-enrolled")}
            || arguments == QStringList{QStringLiteral("--list-new")};
    else if (name == QLatin1String("objdump"))
        allowed = arguments.size() == 2 && arguments.first() == QLatin1String("-f");
    else if (name == QLatin1String("uname"))
        allowed = arguments == QStringList{QStringLiteral("-m")};
    else if (name == QLatin1String("findmnt") || name == QLatin1String("lsblk"))
        allowed = true;
    else if (name == QLatin1String("openssl")) {
        // Only allow certificate inspection; signing/key generation is rejected.
        if (arguments.size() >= 2 && arguments.first() == QLatin1String("x509")) {
            static const QStringList forbidden{
                QStringLiteral("-signkey"), QStringLiteral("-CA"), QStringLiteral("-CAcreateserial"),
                QStringLiteral("-addtrust"), QStringLiteral("-addreject"), QStringLiteral("-set_serial")
            };
            allowed = true;
            for (const QString &arg : arguments) {
                if (forbidden.contains(arg)) {
                    allowed = false;
                    break;
                }
            }
        }
    }
    if (!allowed)
        *error = i18nd("miryu-toolkit", "The command is not an allowed read-only boot probe.");
    return allowed;
}

} // namespace

void BootProbe::stop(QProcess &process)
{
    if (process.state() == QProcess::NotRunning)
        return;
    process.terminate();
    if (process.waitForFinished(1500))
        return;
    process.kill();
    process.waitForFinished(1500);
}

BootProbe::CommandResult BootProbe::readCommand(const QString &program,
                                                const QStringList &arguments,
                                                int timeoutMs)
{
    CommandResult result;
    if (!allowedReadCommand(program, arguments, &result.error))
        return result;

    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.start(program, arguments);
    if (!process.waitForStarted(qMin(2000, timeoutMs))) {
        result.error = process.errorString();
        stop(process);
        return result;
    }
    if (!process.waitForFinished(qMax(1, timeoutMs))) {
        stop(process);
        result.error = i18nd("miryu-toolkit", "Read-only command timed out.");
        return result;
    }
    result.completed = process.exitStatus() == QProcess::NormalExit;
    result.exitCode = process.exitCode();
    result.output = QString::fromUtf8(process.readAllStandardOutput());
    result.error = QString::fromUtf8(process.readAllStandardError());
    return result;
}

QString BootProbe::runReadOnly(const QString &program, const QStringList &arguments)
{
    const CommandResult result = readCommand(program, arguments);
    return result.completed && result.exitCode == 0 ? result.output : QString();
}

BootSnapshot BootProbe::parseEfiBootManager(const QString &output)
{
    BootSnapshot snapshot;
    snapshot.efiBootManagerOutput = output;
    static const QRegularExpression currentPattern(QStringLiteral("^BootCurrent:\\s*([0-9A-Fa-f]{4})$"));
    static const QRegularExpression nextPattern(QStringLiteral("^BootNext:\\s*([0-9A-Fa-f]{4})$"));
    static const QRegularExpression orderPattern(QStringLiteral("^BootOrder:\\s*(.*)$"));
    static const QRegularExpression entryPattern(QStringLiteral("^Boot([0-9A-Fa-f]{4})(\\*)?\\s+(.+)$"));
    static const QRegularExpression gptPattern(QStringLiteral("HD\\([^,]+,GPT,([a-fA-F0-9-]+)"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression filePattern(QStringLiteral("File\\(([^)]+)\\)"),
                                                QRegularExpression::CaseInsensitiveOption);

    for (const auto &raw : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        auto match = currentPattern.match(line);
        if (match.hasMatch()) {
            snapshot.bootCurrent = match.captured(1).toUpper();
            continue;
        }
        match = nextPattern.match(line);
        if (match.hasMatch()) {
            snapshot.bootNext = match.captured(1).toUpper();
            continue;
        }
        match = orderPattern.match(line);
        if (match.hasMatch()) {
            for (const auto &id : match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                if (QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{4}$")).match(id.trimmed()).hasMatch())
                    snapshot.bootOrder.append(id.trimmed().toUpper());
            }
            continue;
        }
        match = entryPattern.match(line);
        if (!match.hasMatch())
            continue;
        const QString rest = match.captured(3);
        const auto pathMatch = filePattern.match(rest);
        const auto guidMatch = gptPattern.match(rest);
        int labelEnd = rest.size();
        for (const auto &token : {QStringLiteral("HD("), QStringLiteral("File("),
                                  QStringLiteral("PciRoot("), QStringLiteral("VenHw(")}) {
            const int position = rest.indexOf(token, 0, Qt::CaseInsensitive);
            if (position >= 0)
                labelEnd = qMin(labelEnd, position);
        }
        FirmwareEntry entry;
        entry.number = match.captured(1).toUpper();
        entry.label = rest.left(labelEnd).trimmed();
        entry.path = pathMatch.hasMatch() ? pathMatch.captured(1) : QString();
        entry.partitionUuid = guidMatch.hasMatch() ? guidMatch.captured(1).toLower() : QString();
        entry.enabled = !match.captured(2).isEmpty();
        snapshot.firmware.append(entry);
    }
    for (auto &entry : snapshot.firmware)
        entry.current = entry.number == snapshot.bootCurrent;
    return snapshot;
}

QList<MokCertificate> BootProbe::parseMokCertificates(const QString &output)
{
    static const QRegularExpression keyHeader(QStringLiteral("^\\[key [0-9]+\\]\\s*$"),
                                              QRegularExpression::MultilineOption);
    static const QRegularExpression fingerprint(QStringLiteral("^\\s*SHA1 Fingerprint:\\s*(\\S+)"),
                                                QRegularExpression::MultilineOption);
    static const QRegularExpression subject(QStringLiteral("^\\s*Subject:\\s*(.*)$"),
                                            QRegularExpression::MultilineOption);
    static const QRegularExpression issuer(QStringLiteral("^\\s*Issuer:\\s*(.*)$"),
                                           QRegularExpression::MultilineOption);
    static const QRegularExpression notAfter(QStringLiteral("^\\s*Not After\\s*:\\s*(.*)$"),
                                             QRegularExpression::MultilineOption);
    static const QRegularExpression commonName(QStringLiteral("CN\\s*=\\s*([^,/]+)"));
    QList<MokCertificate> result;
    QList<qsizetype> starts;
    for (auto iterator = keyHeader.globalMatch(output); iterator.hasNext();)
        starts.append(iterator.next().capturedStart());
    if (starts.isEmpty() && output.contains(QLatin1String("SHA1 Fingerprint")))
        starts.append(0);
    for (int i = 0; i < starts.size(); ++i) {
        const QString block = output.mid(starts.at(i),
            i + 1 < starts.size() ? starts.at(i + 1) - starts.at(i) : -1);
        MokCertificate certificate;
        certificate.fingerprint = fingerprint.match(block).captured(1).trimmed();
        certificate.subject = subject.match(block).captured(1).trimmed();
        certificate.issuer = issuer.match(block).captured(1).trimmed();
        certificate.notAfter = notAfter.match(block).captured(1).trimmed();
        certificate.commonName = commonName.match(certificate.subject).captured(1).trimmed();
        if (certificate.commonName.isEmpty())
            certificate.commonName = certificate.subject;
        if (!certificate.fingerprint.isEmpty() || !certificate.subject.isEmpty())
            result.append(certificate);
    }
    return result;
}

// Parse a single DER-encoded X.509 certificate with openssl and return it in
// the same text layout that mokutil --list-new produces, so parseMokCertificates
// can consume it without changes.
static QString derCertificateToMokText(const QByteArray &der, int keyIndex)
{
    if (der.isEmpty())
        return {};

    QTemporaryFile tempFile(QStringLiteral("miryu-mok-XXXXXX.der"));
    if (!tempFile.open())
        return {};
    tempFile.write(der);
    tempFile.close();

    const BootProbe::CommandResult result = BootProbe::readCommand(
        QStringLiteral("openssl"),
        {QStringLiteral("x509"), QStringLiteral("-inform"), QStringLiteral("DER"),
         QStringLiteral("-in"), tempFile.fileName(),
         QStringLiteral("-noout"), QStringLiteral("-subject"),
         QStringLiteral("-fingerprint"), QStringLiteral("-sha1")});
    if (!result.completed)
        return {};

    QString subject;
    QString fingerprint;
    for (const QString &line : result.output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1String("subject=")))
            subject = line.mid(8).trimmed();
        else if (line.startsWith(QLatin1String("sha1 Fingerprint="), Qt::CaseInsensitive))
            fingerprint = line.section(QLatin1Char('='), 1).trimmed();
    }
    if (subject.isEmpty() && fingerprint.isEmpty())
        return {};

    return QStringLiteral("[key %1]\nSHA1 Fingerprint: %2\nSubject: %3")
        .arg(keyIndex).arg(fingerprint, subject);
}

// Read the MokNew EFI variable directly from efivarfs (world-readable) and
// produce mokutil-style text. mokutil --list-new itself requires root, so the
// non-root scan cannot use it; the variable store is the authoritative source.
static QString readPendingMokFromEfivarfs()
{
    static const QString mokNewPath = QStringLiteral(
        "/sys/firmware/efi/efivars/MokNew-605dab50-e046-4300-abb6-3dd810dd8b23");
    QFile file(mokNewPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QByteArray data = file.readAll();
    file.close();
    if (data.size() <= 4)
        return {};
    data = data.mid(4); // skip the 4-byte EFI variable attributes field

    // EFI_CERT_X509_GUID (a5c059a1-94e4-4aa7-87b5-ab155c2bf072)
    // in little-endian EFI byte order.
    static const QByteArray x509Guid =
        QByteArray::fromHex("a159c0a5e494a74a87b5ab155c2bf072");

    QString output;
    int offset = 0;
    int keyIndex = 1;
    while (offset + 28 <= data.size()) {
        const quint8 *p = reinterpret_cast<const quint8 *>(data.constData()) + offset;
        const quint32 signatureListSize = qFromLittleEndian<quint32>(p + 16);
        const quint32 signatureHeaderSize = qFromLittleEndian<quint32>(p + 20);
        const quint32 signatureSize = qFromLittleEndian<quint32>(p + 24);

        if (signatureListSize < 28 ||
            static_cast<quint32>(data.size()) < offset + signatureListSize) {
            break;
        }

        const QByteArray signatureType(reinterpret_cast<const char *>(p), 16);
        const int headerEnd = offset + 28 + signatureHeaderSize;
        const int listEnd = offset + signatureListSize;

        if (signatureType == x509Guid && signatureSize > 16) {
            int certPos = headerEnd;
            while (certPos + signatureSize <= listEnd) {
                const QByteArray der = data.mid(certPos + 16, signatureSize - 16);
                const QString text = derCertificateToMokText(der, keyIndex);
                if (!text.isEmpty()) {
                    if (!output.isEmpty())
                        output.append(QLatin1Char('\n'));
                    output.append(text);
                    ++keyIndex;
                }
                certPos += signatureSize;
            }
        }
        offset += signatureListSize;
    }
    return output;
}

// mokutil --list-new requires root, so the non-root scan cannot call it
// directly. Instead, invoke the privileged boot helper through the
// org.miryugaming.toolkit.list-mok polkit action, which is allowed for active
// sessions without authentication, and run mokutil --list-new as root.
static QString readPendingMokViaHelper()
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty())
        return {};
    const QString helper = QStringLiteral(MOK_LIST_HELPER_PATH);
    const QFileInfo helperInfo(helper);
    if (!helperInfo.isFile() || !helperInfo.isExecutable())
        return {};

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(pkexec, {QStringLiteral("--disable-internal-agent"),
                           helper, QStringLiteral("--list-mok")});
    if (!process.waitForStarted(10000) || !process.waitForFinished(30000))
        return {};
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAll()).trimmed();
}

QList<EspMount> BootProbe::discoverEspMounts()
{
    QList<EspMount> result;
    QSet<QString> seen;
    const QString output = runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-t"), QStringLiteral("vfat"),
         QStringLiteral("-o"), QStringLiteral("TARGET")});
    for (const auto &raw : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString path = canonicalDirectory(mountUnescape(raw.trimmed()));
        if (path.isEmpty() || seen.contains(path))
            continue;
        const QString actualTarget = canonicalDirectory(mountUnescape(readFindmnt(
            {QStringLiteral("-rn"), QStringLiteral("-M"), path,
             QStringLiteral("-o"), QStringLiteral("TARGET")})));
        if (actualTarget != path)
            continue;
        const QString source = sourceAt(path);
        if (!source.startsWith(QLatin1String("/dev/")))
            continue;
        const QString uuid = runReadOnly(QStringLiteral("lsblk"),
            {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PARTUUID"), source}).trimmed();
        const QString parttype = runReadOnly(QStringLiteral("lsblk"),
            {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PARTTYPE"), source}).trimmed().toLower();
        if (parttype != QLatin1String(espPartitionType)
            && !QDir(path + QStringLiteral("/EFI")).exists()) {
            continue;
        }
        seen.insert(path);
        result.append({path, source, uuid});
    }
    std::sort(result.begin(), result.end(), [](const EspMount &left, const EspMount &right) {
        return left.path < right.path;
    });
    return result;
}

QList<BootDevice> BootProbe::discoverDevices()
{
    const QString output = runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("--json"), QStringLiteral("-p"), QStringLiteral("-o"),
         QStringLiteral("NAME,TYPE,FSTYPE,SIZE,PARTUUID,PARTTYPE,PTTYPE,PKNAME,MODEL,LABEL")});
    const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8());
    QList<BootDevice> result;
    appendDevices(document.object().value(QStringLiteral("blockdevices")).toArray(), &result);
    return result;
}

QList<RepairTarget> BootProbe::mountedLinuxTargets()
{
    QList<RepairTarget> result;
    QSet<QString> seen;
    const QString output = runReadOnly(QStringLiteral("findmnt"),
                                      {QStringLiteral("-rn"), QStringLiteral("-o"), QStringLiteral("TARGET")});
    for (const auto &raw : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString path = canonicalDirectory(mountUnescape(raw.trimmed()));
        if (path.isEmpty() || seen.contains(path))
            continue;
        if (path != QLatin1String("/") && path != QLatin1String("/mnt")
            && !path.startsWith(QLatin1String("/mnt/"))
            && !path.startsWith(QLatin1String("/run/media/"))) {
            continue;
        }
        if (!linuxSystem(path))
            continue;
        RepairTarget target;
        target.root = path;
        target.rootDevice = sourceAt(path);
        target.automatic = false;
        const QString name = operatingSystemName(path);
        target.label = name.isEmpty() ? path : name + QStringLiteral(" · ") + path;
        seen.insert(path);
        result.append(target);
    }
    std::sort(result.begin(), result.end(), [](const RepairTarget &left, const RepairTarget &right) {
        return left.root < right.root;
    });
    return result;
}

QList<RepairTarget> BootProbe::automaticRepairTargets(const QList<BootDevice> &devices,
                                                       const QList<RepairTarget> &mounted)
{
    QList<RepairTarget> result;
    QSet<QString> mountedDevices;
    for (const auto &target : mounted)
        mountedDevices.insert(blockDeviceName(target.rootDevice));
    for (const auto &device : devices) {
        if (device.type != QLatin1String("part")
            || !QStringList{QStringLiteral("btrfs"), QStringLiteral("ext4"),
                            QStringLiteral("xfs"), QStringLiteral("f2fs")}.contains(device.filesystem)
            || mountedDevices.contains(device.path)) {
            continue;
        }
        RepairTarget target;
        target.root = QStringLiteral("auto:") + device.path;
        target.rootDevice = device.path;
        target.automatic = true;
        target.label = device.label();
        result.append(target);
    }
    return result;
}

bool BootProbe::isOnEsp(const QString &path, const QList<EspMount> &mounts)
{
    const QString resolved = QFileInfo(path).canonicalFilePath();
    if (resolved.isEmpty())
        return false;
    for (const auto &mount : mounts) {
        const QString base = QFileInfo(mount.path).canonicalFilePath();
        if (!base.isEmpty() && (resolved == base || resolved.startsWith(base + QLatin1Char('/'))))
            return true;
    }
    return false;
}

bool BootProbe::firmwareAddArguments(const QString &path, const QString &label,
                                     const QList<EspMount> &mounts,
                                     QStringList *arguments, QString *error)
{
    if (!arguments)
        return false;
    arguments->clear();
    const QString cleanLabel = label.trimmed();
    bool validLabel = !label.isEmpty() && label.size() <= 80 && !cleanLabel.isEmpty();
    for (const QChar character : label)
        validLabel = validLabel && character.isPrint();
    const QFileInfo original(path);
    const QString absolute = original.canonicalFilePath();
    if (!validLabel || !QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path
        || original.isSymLink() || absolute.isEmpty() || !QFileInfo(absolute).isFile()
        || !absolute.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)) {
        if (error)
            *error = i18nd("miryu-toolkit", "The EFI file or firmware label is invalid.");
        return false;
    }

    QString mount;
    for (const auto &candidate : mounts) {
        const QString base = canonicalDirectory(candidate.path);
        const QString efiPrefix = base == QLatin1String("/") ? QStringLiteral("/EFI/")
                                                             : base + QLatin1String("/EFI/");
        if (base.isEmpty() || !absolute.startsWith(efiPrefix, Qt::CaseInsensitive))
            continue;
        if (base.size() > mount.size())
            mount = base;
    }
    if (mount.isEmpty()) {
        if (error)
            *error = i18nd("miryu-toolkit", "The EFI binary must be a regular file below %1 on a mounted ESP.",
                          QStringLiteral("EFI/"));
        return false;
    }
    const QString actualTarget = canonicalDirectory(mountUnescape(readFindmnt(
        {QStringLiteral("-rn"), QStringLiteral("-M"), mount,
         QStringLiteral("-o"), QStringLiteral("TARGET")})));
    const QString source = sourceAt(mount);
    const QString filesystem = readFindmnt({QStringLiteral("-rn"), QStringLiteral("-M"), mount,
                                            QStringLiteral("-o"), QStringLiteral("FSTYPE")});
    const QString parttype = runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PARTTYPE"), source}).trimmed();
    if (actualTarget != mount || filesystem.compare(QLatin1String("vfat"), Qt::CaseInsensitive) != 0
        || parttype.compare(QLatin1String(espPartitionType), Qt::CaseInsensitive) != 0) {
        if (error)
            *error = i18nd("miryu-toolkit", "The selected filesystem is not a mounted EFI system partition.");
        return false;
    }

    const QString partition = runReadOnly(QStringLiteral("lsblk"),
        {QStringLiteral("-dn"), QStringLiteral("-o"), QStringLiteral("PKNAME,PARTN"), source}).trimmed();
    const QStringList fields = partition.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (fields.size() != 2
        || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(fields.at(1)).hasMatch()
        || fields.at(1).toInt() <= 0) {
        if (error)
            *error = i18nd("miryu-toolkit", "The physical disk and EFI partition could not be identified.");
        return false;
    }
    const QString disk = fields.first().startsWith(QLatin1String("/dev/"))
        ? QDir::cleanPath(fields.first()) : QStringLiteral("/dev/") + fields.first();
    if (!QFileInfo::exists(disk)) {
        if (error)
            *error = i18nd("miryu-toolkit", "The EFI disk is unavailable.");
        return false;
    }
    QString relative = mount == QLatin1String("/") ? absolute : absolute.mid(mount.size());
    relative.replace(QLatin1Char('/'), QLatin1Char('\\'));
    *arguments = {QStringLiteral("-d"), disk, QStringLiteral("-p"), fields.at(1),
                  QStringLiteral("-L"), cleanLabel, QStringLiteral("-l"), relative};
    if (error)
        error->clear();
    return true;
}

bool BootProbe::firmwareAddArguments(const QString &path, const QString &label,
                                     QStringList *arguments, QString *error)
{
    return firmwareAddArguments(path, label, discoverEspMounts(), arguments, error);
}

bool BootProbe::validateDerCertificate(const QByteArray &certificate, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (certificate.size() < 64 || certificate.size() > 64 * 1024)
        return fail(i18nd("miryu-toolkit", "The certificate must be a DER file between 64 bytes and 64 KiB."));
    if (quint8(certificate.at(0)) != 0x30)
        return fail(i18nd("miryu-toolkit", "The certificate is not an ASN.1 DER SEQUENCE."));
    int offset = 1;
    const quint8 firstLength = quint8(certificate.at(offset++));
    qint64 length = 0;
    if ((firstLength & 0x80U) == 0) {
        length = firstLength;
    } else {
        const int count = firstLength & 0x7fU;
        if (count == 0 || count > 4 || offset + count > certificate.size())
            return fail(i18nd("miryu-toolkit", "The DER certificate has an invalid length."));
        for (int i = 0; i < count; ++i)
            length = (length << 8) | quint8(certificate.at(offset++));
        if (length < 128)
            return fail(i18nd("miryu-toolkit", "The DER certificate uses a non-minimal length encoding."));
    }
    if (length != certificate.size() - offset)
        return fail(i18nd("miryu-toolkit", "The DER certificate is truncated or has trailing data."));
    if (error)
        error->clear();
    return true;
}

QString BootProbe::deviceFingerprint(const QList<BootDevice> &devices)
{
    QStringList records;
    for (const auto &device : devices) {
        records.append(device.path + QLatin1Char('|') + device.type + QLatin1Char('|')
            + device.filesystem + QLatin1Char('|') + device.size + QLatin1Char('|')
            + device.partuuid + QLatin1Char('|') + device.parttype
            + QLatin1Char('|') + device.partitionTable);
    }
    records.sort();
    return QString::fromLatin1(QCryptographicHash::hash(
        records.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString BootProbe::grubTarget(const QString &value)
{
    if (value == QLatin1String("aa64"))
        return QStringLiteral("arm64-efi");
    if (value == QLatin1String("ia32"))
        return QStringLiteral("i386-efi");
    if (value.isEmpty() || value == QLatin1String("x64"))
        return QStringLiteral("x86_64-efi");
    return {};
}

QString BootProbe::grubBiosTarget(const QString &value)
{
    if (value == QLatin1String("x64") || value == QLatin1String("ia32"))
        return QStringLiteral("i386-pc");
    return {};
}

bool BootProbe::validSubvolume(const QString &subvolume)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_@./+-]{0,200}$"));
    if (!allowed.match(subvolume).hasMatch() || subvolume.startsWith(QLatin1Char('/')))
        return false;
    for (const auto &component : subvolume.split(QLatin1Char('/'), Qt::KeepEmptyParts)) {
        if (component == QLatin1String(".."))
            return false;
    }
    return true;
}

QStringList BootProbe::repairDiagnostics(const RepairTarget &target, const BootSnapshot &snapshot,
                                         bool legacyBios)
{
    QStringList messages;
    const BootDevice *rootDevice = nullptr;
    for (const auto &device : snapshot.devices) {
        if (device.path == target.rootDevice) {
            rootDevice = &device;
            break;
        }
    }
    if (!rootDevice) {
        messages.append(i18nd("miryu-toolkit", "Root device is absent from the current disk inventory."));
    } else if (target.automatic
               && (rootDevice->type != QLatin1String("part")
                   || !QStringList{QStringLiteral("btrfs"), QStringLiteral("ext4"),
                                   QStringLiteral("xfs"), QStringLiteral("f2fs")}.contains(rootDevice->filesystem))) {
        messages.append(i18nd("miryu-toolkit", "Automatic repair supports only btrfs, ext4, xfs and f2fs partitions."));
    }
    if (target.automatic && !validSubvolume(target.subvolume))
        messages.append(i18nd("miryu-toolkit", "The Btrfs subvolume name is invalid."));

    if (target.automatic) {
        messages.append(i18nd("miryu-toolkit", "Offline target: GRUB files and firmware paths will be checked after a controlled mount."));
    } else {
        const QString root = QFileInfo(target.root).canonicalFilePath();
        if (root.isEmpty() || !linuxSystem(root)) {
            messages.append(i18nd("miryu-toolkit", "The target does not contain %1 and %2.",
                                 QStringLiteral("etc/os-release"), QStringLiteral("usr/bin/sh")));
            return messages;
        }
        for (const auto &tool : {QStringLiteral("grub2-install"), QStringLiteral("grub2-mkconfig")}) {
            messages.append(executableIn(root, tool).isEmpty()
                ? i18nd("miryu-toolkit", "Missing GRUB tool: %1", tool)
                : i18nd("miryu-toolkit", "GRUB tool found: %1", tool));
        }
        messages.append(QFileInfo(rooted(root, QStringLiteral("etc/default/grub"))).isFile()
            ? i18nd("miryu-toolkit", "GRUB defaults found: %1", QStringLiteral("/etc/default/grub"))
            : i18nd("miryu-toolkit", "GRUB defaults missing: %1", QStringLiteral("/etc/default/grub")));
        messages.append(QFileInfo(rooted(root, QStringLiteral("boot/grub2/grub.cfg"))).isFile()
            ? i18nd("miryu-toolkit", "Generated GRUB configuration found: %1", QStringLiteral("/boot/grub2/grub.cfg"))
            : i18nd("miryu-toolkit", "Generated GRUB configuration not found: %1", QStringLiteral("/boot/grub2/grub.cfg")));

        if (snapshot.uefi && !legacyBios) {
            bool foundEsp = false;
            bool foundEfi = false;
            bool registered = false;
            for (const auto &mount : snapshot.espMounts) {
                const bool inside = root == QLatin1String("/")
                    ? mount.path.startsWith(QLatin1Char('/'))
                    : mount.path.startsWith(root + QLatin1Char('/'));
                if (!inside)
                    continue;
                foundEsp = true;
                QDirIterator files(mount.path + QStringLiteral("/EFI"),
                                   QDir::Files, QDirIterator::Subdirectories);
                while (files.hasNext()) {
                    const QString file = files.next();
                    if (file.endsWith(QLatin1String(".efi"), Qt::CaseInsensitive)
                        && !QFileInfo(file).isSymLink())
                        foundEfi = true;
                }
                for (const auto &entry : snapshot.firmware) {
                    if (!mount.partuuid.isEmpty()
                        && entry.partitionUuid.compare(mount.partuuid, Qt::CaseInsensitive) == 0
                        && !entry.resolvedPath.isEmpty()) {
                        registered = true;
                    }
                }
            }
            if (!foundEsp)
                messages.append(i18nd("miryu-toolkit", "No mounted EFI system partition was found inside the target."));
            if (foundEsp && !foundEfi)
                messages.append(i18nd("miryu-toolkit", "No EFI programs were found below %1 on the target ESP.",
                                     QStringLiteral("EFI/")));
            if (foundEfi && !registered)
                messages.append(i18nd("miryu-toolkit", "No resolvable UEFI firmware entry points to an EFI program on the target ESP."));
            else if (registered)
                messages.append(i18nd("miryu-toolkit", "A UEFI firmware entry resolves to an EFI program on the target ESP."));
        } else {
            const QString modules = QStringLiteral("/usr/lib/grub/i386-pc");
            messages.append(QFileInfo(rooted(root, modules.mid(1))).isDir()
                ? i18nd("miryu-toolkit", "Legacy BIOS GRUB modules found: %1", modules)
                : i18nd("miryu-toolkit", "Legacy BIOS GRUB modules missing: %1", modules));
            const BootDevice *disk = nullptr;
            if (rootDevice) {
                for (const auto &item : snapshot.devices) {
                    if (item.path == rootDevice->parent && item.type == QLatin1String("disk")) {
                        disk = &item;
                        break;
                    }
                }
                if (!disk && rootDevice->type == QLatin1String("disk"))
                    disk = rootDevice;
            }
            if (!disk) {
                messages.append(i18nd("miryu-toolkit", "The disk that contains the target root partition could not be determined."));
            } else {
                messages.append(i18nd("miryu-toolkit", "GRUB will be written to the master boot record of %1.", disk->path));
                bool biosBootPartition = false;
                if (disk->partitionTable == QLatin1String("gpt")) {
                    for (const auto &item : snapshot.devices) {
                        if (item.type == QLatin1String("part") && item.parent == disk->path
                            && item.parttype == QLatin1String("21686148-6449-6e6f-744e-656564454649"))
                            biosBootPartition = true;
                    }
                    if (!biosBootPartition)
                        messages.append(i18nd("miryu-toolkit", "The GPT disk has no BIOS boot partition; %1 will fail.",
                                             QStringLiteral("grub2-install")));
                }
            }
        }
    }
    return messages;
}

BootSnapshot BootProbe::inspect(const QString &root, const Progress &progress)
{
    const auto stage = [&progress](const QString &message) {
        if (progress)
            progress(message);
    };
    BootSnapshot snapshot;
    const QString cleanRoot = QDir::cleanPath(root);
    stage(i18nd("miryu-toolkit", "Reading UEFI firmware state"));
    snapshot.uefi = QDir(rooted(cleanRoot, QStringLiteral("sys/firmware/efi"))).exists();
    snapshot.architecture = architecture();
    if (snapshot.uefi && cleanRoot == QLatin1String("/")) {
        const CommandResult firmware = readCommand(QStringLiteral("efibootmgr"), {QStringLiteral("-v")});
        if (firmware.completed && firmware.exitCode == 0) {
            const BootSnapshot parsed = parseEfiBootManager(firmware.output);
            snapshot.bootCurrent = parsed.bootCurrent;
            snapshot.bootNext = parsed.bootNext;
            snapshot.bootOrder = parsed.bootOrder;
            snapshot.firmware = parsed.firmware;
            snapshot.efiBootManagerOutput = parsed.efiBootManagerOutput;
        } else {
            snapshot.messages.append(i18nd("miryu-toolkit", "Could not read UEFI boot variables with %1.",
                                          QStringLiteral("efibootmgr")));
        }
    } else if (!snapshot.uefi) {
        snapshot.secureBoot = BootSecurityState::Unsupported;
        snapshot.setupMode = BootSecurityState::Unsupported;
        snapshot.messages.append(i18nd("miryu-toolkit", "This system was not booted in UEFI mode."));
    }

    if (snapshot.uefi) {
        snapshot.secureBoot = firmwareVariable(cleanRoot, QStringLiteral("SecureBoot"));
        snapshot.setupMode = firmwareVariable(cleanRoot, QStringLiteral("SetupMode"));
    }
    snapshot.moduleSigEnforce = moduleFlag(rooted(cleanRoot,
        QStringLiteral("sys/module/module/parameters/sig_enforce")));

    if (cleanRoot != QLatin1String("/")) {
        snapshot.messages.append(i18nd("miryu-toolkit", "Device and command probing is performed only for the running root filesystem."));
        return snapshot;
    }

    stage(i18nd("miryu-toolkit", "Finding EFI system partitions and Linux targets"));
    snapshot.espMounts = discoverEspMounts();
    snapshot.devices = discoverDevices();
    snapshot.repairTargets = mountedLinuxTargets();
    snapshot.repairTargets.append(automaticRepairTargets(snapshot.devices, snapshot.repairTargets));
    if (snapshot.uefi && snapshot.espMounts.isEmpty())
        snapshot.messages.append(i18nd("miryu-toolkit", "No mounted EFI system partition was detected."));
    resolveFirmware(&snapshot);

    stage(i18nd("miryu-toolkit", "Reading Secure Boot certificates"));
    if (snapshot.uefi) {
        const CommandResult enrolled = readCommand(QStringLiteral("mokutil"),
                                                   {QStringLiteral("--list-enrolled")});
        if (enrolled.completed && (enrolled.exitCode == 0 || !enrolled.output.isEmpty())) {
            snapshot.enrolledMok = enrolled.output;
            snapshot.enrolledCertificates = parseMokCertificates(enrolled.output);
        } else if (!enrolled.error.trimmed().isEmpty()) {
            snapshot.securityMessages.append(i18nd("miryu-toolkit", "%1: %2", QStringLiteral("mokutil --list-enrolled"),
                                                  enrolled.error.trimmed()));
        }
        // mokutil --list-new requires root. The scan first tries the MokNew
        // EFI variable directly from efivarfs, and if that is not readable by
        // the current user, falls back to the privileged boot helper invoked
        // through the silent org.miryugaming.toolkit.list-mok polkit action.
        QString pendingOutput = readPendingMokFromEfivarfs();
        if (pendingOutput.isEmpty())
            pendingOutput = readPendingMokViaHelper();
        if (!pendingOutput.isEmpty()) {
            snapshot.pendingMok = pendingOutput;
            snapshot.pendingCertificates = parseMokCertificates(pendingOutput);
        }
    }
    return snapshot;
}

namespace {

constexpr quint32 efiVariableAuthenticatedWrite = 0x00000010u;
constexpr quint32 efiVariableTimeBasedAuthenticatedWrite = 0x00000020u;
constexpr quint32 efiLoadOptionActive = 0x00000001u;
constexpr int bootOptionHeaderSize = 10;
constexpr qint64 maximumVariableBytes = 256 * 1024;

bool variableFailure(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool writeVariable(const QString &path, const QByteArray &payload, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable is not writable: %1", path));
    qint64 offset = 0;
    while (offset < payload.size()) {
        const qint64 written = file.write(payload.constData() + offset, payload.size() - offset);
        if (written <= 0) {
            file.close();
            return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable could not be written: %1", path));
        }
        offset += written;
    }
    if (!file.flush()) {
        file.close();
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable could not be flushed: %1", path));
    }
    file.close();
    // Plain files keep trailing bytes when the payload shrinks; efivarfs replaces
    // the whole variable and does not support truncation.
    if (!path.startsWith(QLatin1String("/sys/firmware/efi/efivars"))
        && QFileInfo(path).size() > payload.size()) {
        if (!QFile::resize(path, payload.size()))
            return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable could not be resized: %1", path));
    }
    return true;
}

} // namespace

bool BootProbe::BootVariable::active() const
{
    return (loadOptionAttributes & efiLoadOptionActive) != 0;
}

QString BootProbe::bootVariablePath(const QString &number)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9A-Fa-f]{4}\\z"));
    if (!pattern.match(number).hasMatch())
        return {};
    return QStringLiteral("/sys/firmware/efi/efivars/Boot") + number.toUpper()
        + QStringLiteral("-8be4df61-93ca-11d2-aa0d-00e098032b8c");
}

bool BootProbe::readBootVariable(const QString &path, BootVariable *variable, QString *error)
{
    if (!variable)
        return variableFailure(error, i18nd("miryu-toolkit", "No EFI boot variable destination was provided."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable could not be read: %1", path));
    const QByteArray raw = file.read(maximumVariableBytes);
    if (raw.size() < bootOptionHeaderSize + 2)
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable is too short: %1", path));

    BootVariable parsed;
    parsed.variableAttributes = qFromLittleEndian<quint32>(raw.constData());
    parsed.loadOptionAttributes = qFromLittleEndian<quint32>(raw.constData() + 4);
    parsed.filePathLength = qFromLittleEndian<quint16>(raw.constData() + 8);
    int offset = bootOptionHeaderSize;
    QString description;
    bool terminated = false;
    while (offset + 1 < raw.size()) {
        const quint16 unit = qFromLittleEndian<quint16>(raw.constData() + offset);
        offset += 2;
        if (unit == 0) {
            terminated = true;
            break;
        }
        description.append(QChar(unit));
    }
    if (!terminated)
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable has no description terminator: %1", path));
    if (offset + parsed.filePathLength > raw.size())
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable device path is truncated: %1", path));

    parsed.description = description;
    parsed.devicePathOffset = offset;
    parsed.raw = raw;
    *variable = parsed;
    return true;
}

bool BootProbe::rewriteBootVariableDescription(const QString &path, const QString &description,
                                               QString *error)
{
    BootVariable variable;
    if (!readBootVariable(path, &variable, error))
        return false;
    if (variable.variableAttributes
        & (efiVariableAuthenticatedWrite | efiVariableTimeBasedAuthenticatedWrite)) {
        return variableFailure(error,
            i18nd("miryu-toolkit", "The EFI boot variable requires an authenticated update."));
    }
    if (description.isEmpty() || description.size() > 79)
        return variableFailure(error, i18nd("miryu-toolkit", "The EFI boot variable label is invalid."));

    QByteArray encoded;
    encoded.reserve(description.size() * 2 + 2);
    for (const QChar character : description) {
        encoded.append(static_cast<char>(character.unicode() & 0xff));
        encoded.append(static_cast<char>(character.unicode() >> 8));
    }
    encoded.append('\0');
    encoded.append('\0');

    const QByteArray payload = variable.raw.left(bootOptionHeaderSize) + encoded
        + variable.raw.mid(variable.devicePathOffset);
    return writeVariable(path, payload, error);
}

bool BootProbe::setBootVariableActive(const QString &path, bool active, QString *error)
{
    BootVariable variable;
    if (!readBootVariable(path, &variable, error))
        return false;
    if (variable.variableAttributes
        & (efiVariableAuthenticatedWrite | efiVariableTimeBasedAuthenticatedWrite)) {
        return variableFailure(error,
            i18nd("miryu-toolkit", "The EFI boot variable requires an authenticated update."));
    }
    if (variable.active() == active)
        return true;

    QByteArray payload = variable.raw;
    quint32 attributes = variable.loadOptionAttributes;
    if (active)
        attributes |= efiLoadOptionActive;
    else
        attributes &= ~efiLoadOptionActive;
    qToLittleEndian<quint32>(attributes, payload.data() + 4);
    return writeVariable(path, payload, error);
}

BootSecurityState BootProbe::firmwareVariable(const QString &root, const QString &name)
{
    const QString directory = QDir(root).filePath(QStringLiteral("sys/firmware/efi/efivars"));
    return firmwareFlag(QDir(directory).filePath(
        name + QStringLiteral("-8be4df61-93ca-11d2-aa0d-00e098032b8c")));
}
