#include "bootbackup.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <sys/stat.h>

namespace {

constexpr qint64 maximumManifestBytes = 2 * 1024 * 1024;
constexpr qint64 maximumConfigurationBytes = 1024 * 1024;
constexpr qint64 maximumTotalConfigurationBytes = 8 * 1024 * 1024;
constexpr int maximumFiles = 128;
constexpr auto privateFile = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr auto privateDirectory = privateFile | QFileDevice::ExeOwner;
constexpr auto metadataFile = privateFile | QFileDevice::ReadGroup | QFileDevice::ReadOther;
constexpr auto metadataDirectory = privateDirectory | QFileDevice::ReadGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::ExeOther;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QJsonObject fail(QString *error, const QString &message, const QJsonObject &)
{
    fail(error, message);
    return {};
}

QString digestBytes(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool validId(const QString &id)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    return pattern.match(id).hasMatch();
}

bool validDigest(const QString &digest)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    return pattern.match(digest).hasMatch();
}

bool noSymlinkComponents(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path)
        return false;
    QString current = QStringLiteral("/");
    const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto &component : components) {
        if (current != QLatin1String("/"))
            current += QLatin1Char('/');
        current += component;
        struct stat status {};
        if (::lstat(QFile::encodeName(current).constData(), &status) != 0) {
            if (errno == ENOENT)
                return true;
            return false;
        }
        if (S_ISLNK(status.st_mode))
            return false;
    }
    return true;
}

bool ensureMetadataDirectory(const QString &path, QString *error)
{
    if (!noSymlinkComponents(path))
        return fail(error, i18nd("miryu-toolkit", "The backup path contains a symbolic link or is not absolute."));
    if (!QDir().mkpath(path) || !QFileInfo(path).isDir()
        || !QFile::setPermissions(path, metadataDirectory)) {
        return fail(error, i18nd("miryu-toolkit", "Cannot create a traversable backup directory."));
    }
    return true;
}

bool writePrivate(const QString &path, const QByteArray &data, QString *error)
{
    if (!noSymlinkComponents(path))
        return fail(error, i18nd("miryu-toolkit", "Unsafe backup output path."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(privateFile))
        return fail(error, i18nd("miryu-toolkit", "Cannot open a private backup file for writing."));
    if (file.write(data) != data.size() || !file.commit())
        return fail(error, i18nd("miryu-toolkit", "Cannot atomically write a backup file."));
    return true;
}

bool writeMetadata(const QString &path, const QByteArray &data, QString *error)
{
    if (!noSymlinkComponents(path))
        return fail(error, i18nd("miryu-toolkit", "Unsafe backup metadata output path."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(metadataFile))
        return fail(error, i18nd("miryu-toolkit", "Cannot open backup metadata for writing."));
    if (file.write(data) != data.size() || !file.commit())
        return fail(error, i18nd("miryu-toolkit", "Cannot atomically write backup metadata."));
    return true;
}

QString hashFile(const QString &path, qint64 maximumBytes, bool *ok)
{
    *ok = false;
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() < 0 || info.size() > maximumBytes)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError)
            return {};
        hash.addData(chunk);
    }
    if (file.error() != QFileDevice::NoError)
        return {};
    *ok = true;
    return QString::fromLatin1(hash.result().toHex());
}

QString cleanSourceRoot(const QString &sourceRoot)
{
    const QString clean = QDir::cleanPath(sourceRoot);
    if (clean != sourceRoot || !noSymlinkComponents(clean))
        return {};
    const QFileInfo info(clean);
    if (!info.isDir() || info.isSymLink())
        return {};
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() || !noSymlinkComponents(canonical) ? QString() : canonical;
}

QString sourcePath(const QString &sourceRoot, const QString &logicalPath)
{
    return sourceRoot == QLatin1String("/") ? logicalPath : sourceRoot + logicalPath;
}

bool allowedLogicalPath(const QString &path)
{
    if (path == QLatin1String("/etc/default/grub")
        || path == QLatin1String("/etc/default/grub.d/99_miryu_boot.cfg")) {
        return true;
    }
    static const QRegularExpression bls(
        QStringLiteral("\\A/boot/loader/entries/[^/]{1,255}\\.conf\\z"));
    if (!bls.match(path).hasMatch())
        return false;
    const QString name = path.section(QLatin1Char('/'), -1);
    return name != QLatin1String(".conf") && name != QLatin1String("..conf");
}

bool logicalPath(const QString &sourceRoot, const QString &input, QString *output)
{
    if (!QDir::isAbsolutePath(input))
        return false;
    const QString clean = QDir::cleanPath(input);
    if (clean != input)
        return false;
    QString logical = clean;
    if (sourceRoot != QLatin1String("/") && clean.startsWith(sourceRoot + QLatin1Char('/')))
        logical = clean.mid(sourceRoot.size());
    if (!allowedLogicalPath(logical))
        return false;
    *output = logical;
    return true;
}

bool safeDestination(const QString &sourceRoot, const QString &logical,
                     bool requireParent, QString *error)
{
    const QString path = sourcePath(sourceRoot, logical);
    if (!noSymlinkComponents(path))
        return fail(error, i18nd("miryu-toolkit", "A restore destination contains a symbolic link: %1", logical));
    const QFileInfo info(path);
    if (info.exists() && (!info.isFile() || info.isSymLink()))
        return fail(error, i18nd("miryu-toolkit", "A restore destination is not a regular file: %1", logical));
    const QFileInfo parent(info.absolutePath());
    if (requireParent && (!parent.exists() || !parent.isDir() || parent.isSymLink()))
        return fail(error, i18nd("miryu-toolkit", "A restore destination directory is unavailable: %1", logical));
    return true;
}

QJsonObject targetJson(const RepairTarget &target)
{
    return {{QStringLiteral("root"), target.root},
            {QStringLiteral("rootDevice"), target.rootDevice},
            {QStringLiteral("automatic"), target.automatic},
            {QStringLiteral("subvolume"), target.subvolume},
            {QStringLiteral("label"), target.label}};
}

RepairTarget targetFromJson(const QJsonObject &object)
{
    RepairTarget target;
    target.root = object.value(QStringLiteral("root")).toString();
    target.rootDevice = object.value(QStringLiteral("rootDevice")).toString();
    target.automatic = object.value(QStringLiteral("automatic")).toBool();
    target.subvolume = object.value(QStringLiteral("subvolume")).toString();
    target.label = object.value(QStringLiteral("label")).toString();
    return target;
}

QString withoutMountOptions(QString source)
{
    const int bracket = source.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        source.truncate(bracket);
    return QDir::cleanPath(source);
}

bool validateTarget(const RepairTarget &target, const QString &sourceRoot, QString *error)
{
    if (target.rootDevice.isEmpty() || !target.rootDevice.startsWith(QLatin1String("/dev/")))
        return fail(error, i18nd("miryu-toolkit", "The backup target has no block-device identity."));
    if (target.automatic) {
        if (target.root != QStringLiteral("auto:") + target.rootDevice
            || !BootProbe::validSubvolume(target.subvolume)) {
            return fail(error, i18nd("miryu-toolkit", "The automatic backup target identity is inconsistent."));
        }
    } else {
        const QString selected = cleanSourceRoot(target.root);
        if (selected.isEmpty() || selected != sourceRoot)
            return fail(error, i18nd("miryu-toolkit", "The mounted backup target does not match its recorded root."));
    }
    // Real system mounts have a second identity check. Temporary directory roots
    // used by tests are not findmnt mounts and rely on the explicit device ID.
    const QString mountedSource = withoutMountOptions(BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-M"), sourceRoot,
         QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed());
    if (!mountedSource.isEmpty() && mountedSource != target.rootDevice)
        return fail(error, i18nd("miryu-toolkit", "The target root is now backed by a different block device."));
    return true;
}

QJsonObject readManifest(const QString &root, const QString &id,
                         QByteArray *raw, QString *directory, QString *error)
{
    if (!validId(id) || !noSymlinkComponents(QDir::cleanPath(root)))
        return fail(error, i18nd("miryu-toolkit", "The selected backup ID or root is invalid."), QJsonObject{});
    const QString base = QFileInfo(root).canonicalFilePath();
    if (base.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "The selected backup root is unavailable."), QJsonObject{});
    const QString path = QDir::cleanPath(base + QLatin1Char('/') + id);
    if (!noSymlinkComponents(path) || !QFileInfo(path).isDir() || QFileInfo(path).isSymLink())
        return fail(error, i18nd("miryu-toolkit", "The selected backup is unavailable."), QJsonObject{});
    const QString manifestPath = path + QStringLiteral("/manifest.json");
    QFile file(manifestPath);
    if (!noSymlinkComponents(manifestPath) || QFileInfo(manifestPath).isSymLink()
        || !file.open(QIODevice::ReadOnly)) {
        return fail(error, i18nd("miryu-toolkit", "The selected backup manifest is unavailable."), QJsonObject{});
    }
    *raw = file.read(maximumManifestBytes + 1);
    if (raw->size() > maximumManifestBytes)
        return fail(error, i18nd("miryu-toolkit", "The selected backup manifest is too large."), QJsonObject{});
    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(*raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(QStringLiteral("version")).toInt() != 1) {
        return fail(error, i18nd("miryu-toolkit", "The selected backup manifest is damaged."), QJsonObject{});
    }
    *directory = path;
    if (error)
        error->clear();
    return document.object();
}

struct ValidatedBackup {
    QJsonObject manifest;
    QString directory;
    QString sourceRoot;
    RepairTarget target;
};

bool loadValidated(const QString &root, const QString &id, const QString &expectedDigest,
                   const RepairTarget &requestedTarget, const QString &requestedSource,
                   ValidatedBackup *result, QString *error)
{
    QByteArray raw;
    QString directory;
    const QJsonObject manifest = readManifest(root, id, &raw, &directory, error);
    if (manifest.isEmpty())
        return false;
    if (manifest.value(QStringLiteral("id")).toString() != id)
        return fail(error, i18nd("miryu-toolkit", "The backup manifest ID does not match its directory."));
    if (!validDigest(expectedDigest) || digestBytes(raw).compare(expectedDigest, Qt::CaseInsensitive) != 0)
        return fail(error, i18nd("miryu-toolkit", "The backup changed after it was reviewed. Refresh and try again."));
    const QString sourceRoot = cleanSourceRoot(requestedSource);
    if (sourceRoot.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "The restore target root is unavailable or unsafe."));
    if (!validateTarget(requestedTarget, sourceRoot, error))
        return false;
    const RepairTarget recordedTarget = targetFromJson(manifest.value(QStringLiteral("target")).toObject());
    if (requestedTarget.root != recordedTarget.root
        || requestedTarget.rootDevice != recordedTarget.rootDevice
        || requestedTarget.automatic != recordedTarget.automatic
        || requestedTarget.subvolume != recordedTarget.subvolume) {
        return fail(error, i18nd("miryu-toolkit", "The selected backup belongs to a different target identity."));
    }

    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty() || files.size() > maximumFiles)
        return fail(error, i18nd("miryu-toolkit", "This backup contains no restorable configuration files."));
    QSet<QString> paths;
    qint64 totalBytes = 0;
    for (const auto &value : files) {
        if (!value.isObject())
            return fail(error, i18nd("miryu-toolkit", "The backup contains an invalid file record."));
        const QJsonObject file = value.toObject();
        const QString logical = file.value(QStringLiteral("path")).toString();
        if (!allowedLogicalPath(logical) || paths.contains(logical)
            || !file.value(QStringLiteral("existed")).isBool()) {
            return fail(error, i18nd("miryu-toolkit", "The backup contains a disallowed or duplicate path."));
        }
        paths.insert(logical);
        const int mode = file.value(QStringLiteral("mode")).toInt(-1);
        if (mode < 0 || mode > 0777)
            return fail(error, i18nd("miryu-toolkit", "The backup contains invalid file permissions."));
        if (!safeDestination(sourceRoot, logical, true, error))
            return false;
        if (!file.value(QStringLiteral("existed")).toBool()) {
            if (file.contains(QStringLiteral("backup")))
                return fail(error, i18nd("miryu-toolkit", "A newly-created file record contains an unexpected original."));
            if (file.contains(QStringLiteral("createdSha256"))
                && !validDigest(file.value(QStringLiteral("createdSha256")).toString())) {
                return fail(error, i18nd("miryu-toolkit", "A newly-created file has an invalid post-operation hash."));
            }
            continue;
        }
        const QString backup = file.value(QStringLiteral("backup")).toString();
        static const QRegularExpression savedName(QStringLiteral("^files/[0-9]+\\.original$"));
        const QString sha = file.value(QStringLiteral("sha256")).toString();
        if (!savedName.match(backup).hasMatch() || !validDigest(sha))
            return fail(error, i18nd("miryu-toolkit", "The backup contains an invalid saved-file record."));
        const QString savedPath = directory + QLatin1Char('/') + backup;
        bool ok = false;
        const QString actual = hashFile(savedPath, maximumConfigurationBytes, &ok);
        if (!ok || actual != sha)
            return fail(error, i18nd("miryu-toolkit", "A saved configuration file is damaged: %1", logical));
        totalBytes += QFileInfo(savedPath).size();
        if (totalBytes > maximumTotalConfigurationBytes)
            return fail(error, i18nd("miryu-toolkit", "The saved configuration exceeds the size limit."));
    }
    result->manifest = manifest;
    result->directory = directory;
    result->sourceRoot = sourceRoot;
    result->target = requestedTarget;
    if (error)
        error->clear();
    return true;
}

bool manifestTarget(const QString &root, const QString &id, RepairTarget *target,
                    QString *sourceRoot, QString *digest, QString *error)
{
    QByteArray raw;
    QString directory;
    const QJsonObject manifest = readManifest(root, id, &raw, &directory, error);
    if (manifest.isEmpty())
        return false;
    *target = targetFromJson(manifest.value(QStringLiteral("target")).toObject());
    *sourceRoot = target->root;
    *digest = digestBytes(raw);
    if (target->automatic)
        return fail(error, i18nd("miryu-toolkit", "An offline backup requires an explicit mounted restore target."));
    return true;
}

QJsonArray firmwareJson(const BootSnapshot &snapshot)
{
    QJsonArray result;
    for (const auto &entry : snapshot.firmware) {
        result.append(QJsonObject{{QStringLiteral("number"), entry.number},
            {QStringLiteral("label"), entry.label}, {QStringLiteral("path"), entry.path},
            {QStringLiteral("partitionUuid"), entry.partitionUuid},
            {QStringLiteral("enabled"), entry.enabled}, {QStringLiteral("current"), entry.current},
            {QStringLiteral("resolvedPath"), entry.resolvedPath}});
    }
    return result;
}

} // namespace

QJsonObject BackupInfo::json() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("created"), created},
            {QStringLiteral("phase"), phase}, {QStringLiteral("targetRoot"), targetRoot},
            {QStringLiteral("rootDevice"), rootDevice}, {QStringLiteral("directory"), directory},
            {QStringLiteral("digest"), digest}, {QStringLiteral("operations"), operations},
            {QStringLiteral("files"), files}};
}

QStringList BootBackup::configurationPaths(const QString &sourceRoot)
{
    QStringList result{QStringLiteral("/etc/default/grub"),
                       QStringLiteral("/etc/default/grub.d/99_miryu_boot.cfg")};
    const QDir entries(sourcePath(sourceRoot, QStringLiteral("/boot/loader/entries")));
    for (const auto &name : entries.entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name))
        result.append(QStringLiteral("/boot/loader/entries/") + name);
    return result;
}

QList<BackupInfo> BootBackup::infos(const QString &root, int limit)
{
    QList<BackupInfo> found;
    if (limit <= 0 || !QFileInfo(root).isDir())
        return found;
    const QDir directory(root);
    for (const auto &id : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QByteArray raw;
        QString backupDirectory;
        QString error;
        const QJsonObject manifest = readManifest(root, id, &raw, &backupDirectory, &error);
        if (!error.isEmpty())
            continue;
        BackupInfo info;
        info.id = id;
        info.created = manifest.value(QStringLiteral("created")).toString();
        info.phase = manifest.value(QStringLiteral("phase")).toString();
        const RepairTarget target = targetFromJson(manifest.value(QStringLiteral("target")).toObject());
        info.targetRoot = target.root;
        info.rootDevice = target.rootDevice;
        info.directory = backupDirectory;
        info.digest = digestBytes(raw);
        QJsonArray descriptions = manifest.value(QStringLiteral("operations")).toArray();
        if (descriptions.isEmpty()) {
            for (const auto &result : manifest.value(QStringLiteral("results")).toArray())
                descriptions.append(result.toObject().value(QStringLiteral("action")).toString());
        }
        info.operations = descriptions;
        for (const auto &value : manifest.value(QStringLiteral("files")).toArray()) {
            const QJsonObject file = value.toObject();
            info.files.append(QJsonObject{{QStringLiteral("path"), file.value(QStringLiteral("path"))},
                                          {QStringLiteral("existed"), file.value(QStringLiteral("existed"))}});
        }
        found.append(info);
    }
    std::sort(found.begin(), found.end(), [](const BackupInfo &left, const BackupInfo &right) {
        return left.created > right.created;
    });
    if (found.size() > limit)
        found.erase(found.begin() + limit, found.end());
    return found;
}

QJsonArray BootBackup::list(const QString &root, int limit)
{
    QJsonArray result;
    const QList<BackupInfo> items = infos(root, limit);
    for (const auto &item : items)
        result.append(item.json());
    return result;
}

QStringList BootBackup::restorePaths(const QString &root, const QString &id, QString *error)
{
    QByteArray raw;
    QString directory;
    const QJsonObject manifest = readManifest(root, id, &raw, &directory, error);
    if (manifest.isEmpty())
        return {};
    QStringList result;
    for (const auto &value : manifest.value(QStringLiteral("files")).toArray()) {
        const QString path = value.toObject().value(QStringLiteral("path")).toString();
        if (!allowedLogicalPath(path)) {
            fail(error, i18nd("miryu-toolkit", "The backup contains a disallowed restore path."));
            return {};
        }
        result.append(path);
    }
    if (error)
        error->clear();
    return result;
}

bool BootBackup::validateRestore(const QString &root, const QString &id,
                                 const QString &digest, const RepairTarget &target,
                                 const QString &sourceRoot, QString *error)
{
    ValidatedBackup validated;
    return loadValidated(root, id, digest, target, sourceRoot, &validated, error);
}

bool BootBackup::validateRestore(const QString &root, const QString &id,
                                 const QString &digest, QString *error)
{
    RepairTarget target;
    QString sourceRoot;
    QString actualDigest;
    if (!manifestTarget(root, id, &target, &sourceRoot, &actualDigest, error))
        return false;
    return validateRestore(root, id, digest, target, sourceRoot, error);
}

bool BootBackup::create(const BootSnapshot &snapshot, const RepairTarget &target,
                        const QString &sourceRoot, const QStringList &paths,
                        const QString &backupRoot, QString *error)
{
    directory_.clear();
    id_.clear();
    sourceRoot_ = cleanSourceRoot(sourceRoot);
    target_ = target;
    manifest_ = {};
    if (sourceRoot_.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "The backup source root is unavailable or unsafe."));
    if (paths.size() > maximumFiles)
        return fail(error, i18nd("miryu-toolkit", "Too many configuration files were selected for backup."));
    if (!paths.isEmpty() && !validateTarget(target_, sourceRoot_, error))
        return false;
    if (!ensureMetadataDirectory(backupRoot, error))
        return false;

    id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    directory_ = QDir(backupRoot).filePath(id_);
    if (!QDir().mkdir(directory_) || !QFile::setPermissions(directory_, metadataDirectory)
        || !QDir().mkdir(directory_ + QStringLiteral("/files"))
        || !QFile::setPermissions(directory_ + QStringLiteral("/files"), privateDirectory)) {
        return fail(error, i18nd("miryu-toolkit", "Cannot create a private transaction backup."));
    }

    QSet<QString> unique;
    QJsonArray files;
    qint64 totalBytes = 0;
    for (const auto &input : paths) {
        QString logical;
        if (!logicalPath(sourceRoot_, input, &logical) || unique.contains(logical))
            return fail(error, i18nd("miryu-toolkit", "A configuration path is outside the backup whitelist or duplicated: %1", input));
        unique.insert(logical);
        if (!safeDestination(sourceRoot_, logical, false, error))
            return false;
        const QString originalPath = sourcePath(sourceRoot_, logical);
        const QFileInfo info(originalPath);
        QJsonObject file{{QStringLiteral("path"), logical},
                         {QStringLiteral("existed"), info.exists()},
                         {QStringLiteral("sha256"), QString()},
                         {QStringLiteral("mode"), info.exists() ? int(info.permissions() & 0777) : 0644}};
        if (info.exists()) {
            if (!info.isFile() || info.isSymLink() || info.size() > maximumConfigurationBytes)
                return fail(error, i18nd("miryu-toolkit", "An unsafe or oversized configuration backup source: %1", logical));
            totalBytes += info.size();
            if (totalBytes > maximumTotalConfigurationBytes)
                return fail(error, i18nd("miryu-toolkit", "The configuration backup exceeds the total size limit."));
            QFile source(originalPath);
            if (!source.open(QIODevice::ReadOnly))
                return fail(error, i18nd("miryu-toolkit", "Cannot read configuration for backup: %1", logical));
            const QByteArray data = source.read(maximumConfigurationBytes + 1);
            if (data.size() != info.size() || data.size() > maximumConfigurationBytes)
                return fail(error, i18nd("miryu-toolkit", "Configuration changed or could not be read: %1", logical));
            const QString savedName = QStringLiteral("files/") + QString::number(files.size())
                + QStringLiteral(".original");
            if (!writePrivate(directory_ + QLatin1Char('/') + savedName, data, error))
                return false;
            file.insert(QStringLiteral("backup"), savedName);
            file.insert(QStringLiteral("sha256"), digestBytes(data));
        }
        files.append(file);
    }

    QString firmwareOutput = snapshot.efiBootManagerOutput;
    if (firmwareOutput.isEmpty() && snapshot.uefi)
        firmwareOutput = BootProbe::runReadOnly(QStringLiteral("efibootmgr"), {QStringLiteral("-v")});
    if (!writePrivate(directory_ + QStringLiteral("/efibootmgr.txt"), firmwareOutput.toUtf8(), error)
        || !writePrivate(directory_ + QStringLiteral("/operations.log"), QByteArray(), error)) {
        return false;
    }

    manifest_ = {{QStringLiteral("version"), 1},
        {QStringLiteral("id"), id_},
        {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("phase"), QStringLiteral("prepared")},
        {QStringLiteral("target"), targetJson(target_)},
        {QStringLiteral("files"), files}, {QStringLiteral("firmware"), firmwareJson(snapshot)},
        {QStringLiteral("bootCurrent"), snapshot.bootCurrent},
        {QStringLiteral("bootNext"), snapshot.bootNext},
        {QStringLiteral("bootOrder"), QJsonArray::fromStringList(snapshot.bootOrder)},
        {QStringLiteral("architecture"), snapshot.architecture},
        {QStringLiteral("operations"), QJsonArray{}}, {QStringLiteral("results"), QJsonArray{}},
        {QStringLiteral("scope"), QStringLiteral("Whitelisted configuration originals and observed firmware state; generated grub.cfg, EFI binaries and raw NVRAM are never restored.")}};
    return save(error);
}

bool BootBackup::create(const BootSnapshot &snapshot, const QString &targetRoot,
                        const QString &rootDevice, const QStringList &paths,
                        const QString &backupRoot, QString *error)
{
    RepairTarget target;
    target.root = targetRoot;
    target.rootDevice = rootDevice;
    target.automatic = targetRoot.startsWith(QLatin1String("auto:/dev/"));
    return create(snapshot, target, target.automatic ? QString() : targetRoot,
                  paths, backupRoot, error);
}

bool BootBackup::save(QString *error)
{
    if (directory_.isEmpty() || !noSymlinkComponents(directory_))
        return fail(error, i18nd("miryu-toolkit", "The transaction backup directory is unsafe."));
    const QByteArray data = QJsonDocument(manifest_).toJson(QJsonDocument::Indented);
    if (data.size() > maximumManifestBytes)
        return fail(error, i18nd("miryu-toolkit", "The transaction manifest is too large."));
    return writeMetadata(directory_ + QStringLiteral("/manifest.json"), data, error);
}

bool BootBackup::captureCreatedFiles(QString *error)
{
    if (sourceRoot_.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "The transaction backup is not initialized."));
    // An automatic target may already have been unmounted when finish() runs.
    // Keep fingerprints captured by the last record() rather than treating the
    // empty mountpoint as proof that its files were deleted.
    if (target_.automatic
        && !QFileInfo::exists(sourcePath(sourceRoot_, QStringLiteral("/etc/os-release"))))
        return true;
    QJsonArray files = manifest_.value(QStringLiteral("files")).toArray();
    for (int i = 0; i < files.size(); ++i) {
        QJsonObject file = files.at(i).toObject();
        if (file.value(QStringLiteral("existed")).toBool())
            continue;
        const QString logical = file.value(QStringLiteral("path")).toString();
        const QString path = sourcePath(sourceRoot_, logical);
        if (!noSymlinkComponents(path))
            return fail(error, i18nd("miryu-toolkit", "A newly-created configuration path became unsafe."));
        const QFileInfo info(path);
        if (!info.exists()) {
            file.remove(QStringLiteral("createdSha256"));
        } else {
            if (!info.isFile() || info.isSymLink())
                return fail(error, i18nd("miryu-toolkit", "A newly-created configuration is not a regular file."));
            bool ok = false;
            const QString hash = hashFile(path, maximumConfigurationBytes, &ok);
            if (!ok)
                return fail(error, i18nd("miryu-toolkit", "Cannot fingerprint a newly-created configuration file."));
            file.insert(QStringLiteral("createdSha256"), hash);
        }
        files.replace(i, file);
    }
    manifest_.insert(QStringLiteral("files"), files);
    return true;
}

bool BootBackup::record(const QString &action, bool success, const QString &details, QString *error)
{
    if (directory_.isEmpty() || action.isEmpty() || action.size() > 4096 || details.size() > 16384)
        return fail(error, i18nd("miryu-toolkit", "Invalid operation record."));
    if (!captureCreatedFiles(error))
        return false;
    const QJsonObject result{{QStringLiteral("action"), action},
        {QStringLiteral("success"), success},
        {QStringLiteral("details"), details},
        {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    QJsonArray results = manifest_.value(QStringLiteral("results")).toArray();
    results.append(result);
    manifest_.insert(QStringLiteral("results"), results);
    manifest_.insert(QStringLiteral("phase"), success ? QStringLiteral("applying") : QStringLiteral("incomplete"));
    const QString logPath = directory_ + QStringLiteral("/operations.log");
    QFile log(logPath);
    if (!noSymlinkComponents(logPath) || QFileInfo(logPath).isSymLink()
        || QFileInfo(logPath).size() > maximumManifestBytes
        || !log.open(QIODevice::WriteOnly | QIODevice::Append)
        || !log.setPermissions(privateFile)) {
        return fail(error, i18nd("miryu-toolkit", "Cannot append to the operation log."));
    }
    const QByteArray line = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    if (log.write(line) != line.size())
        return fail(error, i18nd("miryu-toolkit", "Cannot append to the operation log."));
    log.close();
    return save(error);
}

bool BootBackup::record(const QString &action, bool success, QString *error)
{
    return record(action, success, QString(), error);
}

bool BootBackup::finish(bool success, QString *error)
{
    if (directory_.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "The transaction backup is not initialized."));
    if (!captureCreatedFiles(error))
        return false;
    manifest_.insert(QStringLiteral("phase"), success ? QStringLiteral("completed") : QStringLiteral("incomplete"));
    return save(error);
}

QString BootBackup::digest() const
{
    QFile file(directory_ + QStringLiteral("/manifest.json"));
    if (directory_.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.read(maximumManifestBytes + 1);
    return data.size() <= maximumManifestBytes ? digestBytes(data) : QString();
}

bool BootBackup::restore(const QString &root, const QString &id, const QString &digest,
                         const RepairTarget &target, const QString &sourceRoot, QString *error)
{
    ValidatedBackup validated;
    if (!loadValidated(root, id, digest, target, sourceRoot, &validated, error))
        return false;
    const QJsonArray files = validated.manifest.value(QStringLiteral("files")).toArray();
    for (const auto &value : files) {
        const QJsonObject file = value.toObject();
        const QString logical = file.value(QStringLiteral("path")).toString();
        const QString destination = sourcePath(validated.sourceRoot, logical);
        if (!file.value(QStringLiteral("existed")).toBool()) {
            if (!QFileInfo::exists(destination))
                continue;
            const QString createdHash = file.value(QStringLiteral("createdSha256")).toString();
            if (!validDigest(createdHash))
                return fail(error, i18nd("miryu-toolkit", "Refusing to remove a configuration not fingerprinted by the transaction: %1", logical));
            bool ok = false;
            const QString current = hashFile(destination, maximumConfigurationBytes, &ok);
            if (!ok)
                return fail(error, i18nd("miryu-toolkit", "Cannot verify a configuration created by the transaction: %1", logical));
            if (current != createdHash)
                return fail(error, i18nd("miryu-toolkit", "Refusing to remove a configuration changed after the transaction: %1", logical));
            if (!QFile::remove(destination))
                return fail(error, i18nd("miryu-toolkit", "Cannot remove a file created by the transaction: %1", logical));
            continue;
        }

        const QString savedPath = validated.directory + QLatin1Char('/')
            + file.value(QStringLiteral("backup")).toString();
        QFile saved(savedPath);
        if (!saved.open(QIODevice::ReadOnly))
            return fail(error, i18nd("miryu-toolkit", "Cannot reopen a saved configuration: %1", logical));
        const QByteArray data = saved.read(maximumConfigurationBytes + 1);
        if (data.size() > maximumConfigurationBytes
            || digestBytes(data) != file.value(QStringLiteral("sha256")).toString()) {
            return fail(error, i18nd("miryu-toolkit", "A saved configuration changed before restoration: %1", logical));
        }
        if (!safeDestination(validated.sourceRoot, logical, true, error))
            return false;
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size()
            || !output.commit()) {
            return fail(error, i18nd("miryu-toolkit", "Cannot atomically restore configuration: %1", logical));
        }
        if (!QFile::setPermissions(destination,
                QFileDevice::Permissions(file.value(QStringLiteral("mode")).toInt() & 0777))) {
            return fail(error, i18nd("miryu-toolkit", "Cannot restore configuration permissions: %1", logical));
        }
    }
    if (error)
        error->clear();
    return true;
}

bool BootBackup::restore(const QString &root, const QString &id,
                         const RepairTarget &target, const QString &sourceRoot, QString *error)
{
    QByteArray raw;
    QString directory;
    if (readManifest(root, id, &raw, &directory, error).isEmpty())
        return false;
    return restore(root, id, digestBytes(raw), target, sourceRoot, error);
}

bool BootBackup::restore(const QString &root, const QString &id, QString *error)
{
    RepairTarget target;
    QString sourceRoot;
    QString digest;
    if (!manifestTarget(root, id, &target, &sourceRoot, &digest, error))
        return false;
    return restore(root, id, digest, target, sourceRoot, error);
}
