#include "bootbackup.h"
#include "bootdata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include <KLocalizedString>

#include <memory>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr qint64 maximumRequestBytes = 256 * 1024;
const QString espPartitionType = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");

void output(const QString &message)
{
    QTextStream(stdout) << message << Qt::endl;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString executable(const QString &name)
{
    return QStandardPaths::findExecutable(name,
        {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"),
         QStringLiteral("/sbin"), QStringLiteral("/bin")});
}

QProcessEnvironment commandEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("PATH"),
        QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    return environment;
}

bool runCommand(const QString &program, const QStringList &arguments, int timeoutMs,
                QString *error, const QByteArray &input = {}, bool log = true)
{
    if (log)
        output(QFileInfo(program).fileName() + QLatin1Char(' ') + arguments.join(QLatin1Char(' ')));
    QProcess process;
    process.setProcessEnvironment(commandEnvironment());
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) {
        BootProbe::stop(process);
        return fail(error, i18nd("miryu-toolkit", "Could not start %1.", QFileInfo(program).fileName()));
    }
    if (!input.isEmpty()) {
        qint64 offset = 0;
        while (offset < input.size()) {
            const qint64 written = process.write(input.mid(offset));
            if (written <= 0) {
                BootProbe::stop(process);
                return fail(error, i18nd("miryu-toolkit", "Could not provide standard input to a privileged operation."));
            }
            offset += written;
        }
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        BootProbe::stop(process);
        return fail(error, i18nd("miryu-toolkit", "%1 timed out.", QFileInfo(program).fileName()));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail(error, i18nd("miryu-toolkit", "%1 did not complete successfully.", QFileInfo(program).fileName()));
    return true;
}

QString sanitize(QString text, const QString &secret)
{
    if (!secret.isEmpty())
        text.replace(secret, QStringLiteral("[redacted]"));
    return text;
}

QString rootPath(const QString &root, const QString &path)
{
    return root == QLatin1String("/") ? path : root + path;
}

QString cleanDeviceSource(QString source)
{
    const int bracket = source.indexOf(QLatin1Char('['));
    if (bracket >= 0)
        source.truncate(bracket);
    return QDir::cleanPath(source);
}

QString mountSource(const QString &path)
{
    return cleanDeviceSource(BootProbe::runReadOnly(QStringLiteral("findmnt"),
        {QStringLiteral("-rn"), QStringLiteral("-M"), path,
         QStringLiteral("-o"), QStringLiteral("SOURCE")}).trimmed());
}

bool containedFile(const QString &root, const QString &path)
{
    const QString base = QFileInfo(root).canonicalFilePath();
    const QString resolved = QFileInfo(path).canonicalFilePath();
    return !base.isEmpty() && !resolved.isEmpty() && QFileInfo(resolved).isFile()
        && (base == QLatin1String("/") || resolved.startsWith(base + QLatin1Char('/')));
}

bool validLinuxRoot(const QString &root)
{
    return !QFileInfo(root).canonicalFilePath().isEmpty()
        && containedFile(root, rootPath(root, QStringLiteral("/etc/os-release")))
        && containedFile(root, rootPath(root, QStringLiteral("/usr/bin/sh")));
}

QString targetExecutable(const QString &root, const QString &name)
{
    if (root == QLatin1String("/")) {
        const QString host = executable(name);
        if (!host.isEmpty())
            return host;
    }
    for (const auto &directory : {QStringLiteral("/usr/sbin/"), QStringLiteral("/usr/bin/"),
                                  QStringLiteral("/sbin/"), QStringLiteral("/bin/")}) {
        const QString candidate = rootPath(root, directory + name);
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable() && containedFile(root, candidate))
            return info.canonicalFilePath();
    }
    return {};
}

QString findGrubModuleDirectory(const QString &root, const QString &architecture)
{
    static const QStringList bases = {
        QStringLiteral("/usr/lib/grub/"),
        QStringLiteral("/usr/lib64/grub/"),
        QStringLiteral("/lib/grub/"),
        QStringLiteral("/usr/local/lib/grub/")
    };
    for (const auto &base : bases) {
        const QDir directory(rootPath(root, base + architecture));
        if (!directory.exists())
            continue;
        if (!directory.entryList({QStringLiteral("*.mod")}, QDir::Files).isEmpty())
            return directory.canonicalPath();
    }
    return {};
}

bool noSymlinks(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path)
        return false;
    QString current = QStringLiteral("/");
    for (const auto &component : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
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

bool mountPoint(const QString &root, const QString &relative, bool create, QString *error)
{
    if (!relative.startsWith(QLatin1Char('/')) || relative.contains(QLatin1String("..")))
        return fail(error, i18nd("miryu-toolkit", "Invalid mount point below the target root."));
    const QString path = rootPath(root, relative);
    if (!noSymlinks(path))
        return fail(error, i18nd("miryu-toolkit", "A mount point contains a symbolic link: %1", relative));
    if (create && !QDir().mkpath(path))
        return fail(error, i18nd("miryu-toolkit", "Cannot create mount point: %1", relative));
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return fail(error, i18nd("miryu-toolkit", "Mount point is not a directory: %1", relative));
    const QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || (root != QLatin1String("/") && !canonical.startsWith(root + QLatin1Char('/'))))
        return fail(error, i18nd("miryu-toolkit", "Mount point escapes the selected target root."));
    return true;
}

bool checkSeparateBoot(const QString &root, const QString &espRelative, QString *error)
{
    if (espRelative == QLatin1String("/boot"))
        return true;
    QFile fstab(rootPath(root, QStringLiteral("/etc/fstab")));
    if (!fstab.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    if (fstab.size() > 1024 * 1024)
        return fail(error, i18nd("miryu-toolkit", "The target fstab is too large to validate."));
    while (!fstab.atEnd()) {
        const QString line = QString::fromUtf8(fstab.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (fields.size() >= 2 && fields.at(1) == QLatin1String("/boot")) {
            const QString boot = rootPath(root, QStringLiteral("/boot"));
            if (mountSource(boot).isEmpty())
                return fail(error, i18nd("miryu-toolkit", "The target has a separate /boot filesystem; mount it before repair."));
        }
    }
    return true;
}

RepairTarget requestTarget(const QJsonObject &request)
{
    const QJsonObject data = request.value(QStringLiteral("target")).toObject();
    RepairTarget target;
    target.root = data.value(QStringLiteral("root")).toString();
    target.rootDevice = data.value(QStringLiteral("rootDevice")).toString();
    target.automatic = data.value(QStringLiteral("automatic")).toBool(false);
    target.subvolume = data.value(QStringLiteral("subvolume")).toString();
    target.label = data.value(QStringLiteral("label")).toString();
    return target;
}

const BootDevice *device(const BootSnapshot &snapshot, const QString &path)
{
    for (const auto &item : snapshot.devices) {
        if (item.path == path)
            return &item;
    }
    return nullptr;
}

bool validateTarget(const RepairTarget &target, const BootSnapshot &snapshot, QString *error)
{
    if (!target.rootDevice.startsWith(QLatin1String("/dev/"))
        || QDir::cleanPath(target.rootDevice) != target.rootDevice
        || !BootProbe::validSubvolume(target.subvolume)) {
        return fail(error, i18nd("miryu-toolkit", "The target block device or subvolume is invalid."));
    }
    const BootDevice *root = device(snapshot, target.rootDevice);
    if (target.automatic) {
        if (target.root != QStringLiteral("auto:") + target.rootDevice || !root
            || root->type != QLatin1String("part")
            || !QStringList{QStringLiteral("btrfs"), QStringLiteral("ext4"),
                            QStringLiteral("xfs"), QStringLiteral("f2fs")}.contains(root->filesystem)
            || (root->filesystem != QLatin1String("btrfs") && !target.subvolume.isEmpty())) {
            return fail(error, i18nd("miryu-toolkit", "Automatic repair requires a supported Linux partition and Btrfs subvolume."));
        }
        for (const auto &mounted : snapshot.repairTargets) {
            if (!mounted.automatic && mounted.rootDevice == target.rootDevice)
                return fail(error, i18nd("miryu-toolkit", "The selected automatic target is already mounted; select its mounted root instead."));
        }
        return true;
    }

    if (target.root.isEmpty() || !QDir::isAbsolutePath(target.root)
        || QDir::cleanPath(target.root) != target.root || !noSymlinks(target.root)) {
        return fail(error, i18nd("miryu-toolkit", "The mounted target root is invalid."));
    }
    const QString canonical = QFileInfo(target.root).canonicalFilePath();
    bool listed = false;
    for (const auto &candidate : snapshot.repairTargets) {
        if (!candidate.automatic && candidate.rootDevice == target.rootDevice
            && QFileInfo(candidate.root).canonicalFilePath() == canonical) {
            listed = true;
            break;
        }
    }
    if (!listed || canonical.isEmpty() || !validLinuxRoot(canonical)
        || mountSource(canonical) != target.rootDevice) {
        return fail(error, i18nd("miryu-toolkit", "The mounted Linux target is not in the allowed inventory or its mount source changed."));
    }
    return true;
}

struct RepairRequest {
    RepairTarget target;
    QString mode;
    QString disk;
    QString espPath;
    QString espDevice;
    QString espRelative;
    QString existingEspMount;
    QString bootloaderId;
    QString architecture;
    QString grubArchitecture;
    bool registerEfi = false;
    bool fallback = false;
};

bool validateRepairDisk(const QString &diskPath, const RepairTarget &target,
                        const BootSnapshot &snapshot, QString *error)
{
    if (!diskPath.startsWith(QLatin1String("/dev/"))
        || QDir::cleanPath(diskPath) != diskPath
        || diskPath.startsWith(QLatin1String("/dev/zram"))
        || diskPath.startsWith(QLatin1String("/dev/loop")))
        return fail(error, i18nd("miryu-toolkit", "The GRUB installation disk path is invalid."));
    const BootDevice *disk = device(snapshot, diskPath);
    if (!disk || disk->type != QLatin1String("disk"))
        return fail(error, i18nd("miryu-toolkit", "The GRUB installation target must be a whole disk."));
    const BootDevice *root = device(snapshot, target.rootDevice);
    if (!root)
        return fail(error, i18nd("miryu-toolkit", "The target root device is absent from the disk inventory."));
    if (root->parent != diskPath && target.rootDevice != diskPath)
        return fail(error, i18nd("miryu-toolkit", "The selected disk does not contain the target root partition."));
    return true;
}

bool parseRepair(const QJsonObject &request, const BootSnapshot &snapshot,
                 RepairRequest *result, QString *error)
{
    RepairRequest data;
    const QString loader = request.value(QStringLiteral("loader")).toString(QStringLiteral("grub"));
    if (loader != QLatin1String("grub") && loader != QLatin1String("grub2"))
        return fail(error, i18nd("miryu-toolkit", "Only GRUB2 boot repair is supported."));
    data.mode = request.value(QStringLiteral("mode")).toString(QStringLiteral("uefi"));
    if (data.mode != QLatin1String("uefi") && data.mode != QLatin1String("bios"))
        return fail(error, i18nd("miryu-toolkit", "Only GRUB2 UEFI or legacy BIOS repair is supported."));
    data.target = requestTarget(request);
    data.espPath = request.value(QStringLiteral("espPath")).toString();
    data.espDevice = request.value(QStringLiteral("espDevice")).toString();
    data.espRelative = request.value(QStringLiteral("espRelative")).toString();
    data.bootloaderId = request.value(QStringLiteral("bootloaderId")).toString();
    data.architecture = request.value(QStringLiteral("architecture")).toString();
    data.registerEfi = request.value(QStringLiteral("register")).toBool(false);
    data.fallback = request.value(QStringLiteral("fallback")).toBool(false);
    if (!validateTarget(data.target, snapshot, error))
        return false;
    if (request.value(QStringLiteral("expectedDeviceFingerprint")).toString()
        != BootProbe::deviceFingerprint(snapshot.devices)) {
        return fail(error, i18nd("miryu-toolkit", "The disk inventory changed before repair was applied."));
    }
    if (data.mode == QLatin1String("bios")) {
        if (data.registerEfi || data.fallback)
            return fail(error, i18nd("miryu-toolkit", "Legacy BIOS repair does not register firmware entries or fallback loaders."));
        if (!data.espPath.isEmpty() || !data.espDevice.isEmpty() || !data.espRelative.isEmpty())
            return fail(error, i18nd("miryu-toolkit", "Legacy BIOS repair does not use an EFI system partition."));
        data.disk = request.value(QStringLiteral("disk")).toString();
        if (!validateRepairDisk(data.disk, data.target, snapshot, error))
            return false;
        data.architecture = snapshot.architecture;
        data.grubArchitecture = BootProbe::grubBiosTarget(data.architecture);
        if (data.grubArchitecture.isEmpty())
            return fail(error, i18nd("miryu-toolkit", "Legacy BIOS repair is available on x86 systems only."));
        if (data.target.automatic
            && (executable(QStringLiteral("mount")).isEmpty()
                || executable(QStringLiteral("umount")).isEmpty())) {
            return fail(error, i18nd("miryu-toolkit", "mount and umount are required for automatic repair."));
        }
        if (!data.target.automatic) {
            const QString root = QFileInfo(data.target.root).canonicalFilePath();
            if (!checkSeparateBoot(root, QString(), error))
                return false;
        }
        *result = data;
        return true;
    }
    const BootDevice *esp = device(snapshot, data.espDevice);
    if (!esp || esp->type != QLatin1String("part")
        || esp->filesystem.compare(QLatin1String("vfat"), Qt::CaseInsensitive) != 0
        || esp->parttype.compare(espPartitionType, Qt::CaseInsensitive) != 0
        || data.espDevice == data.target.rootDevice
        || !QStringList{QStringLiteral("/boot/efi"), QStringLiteral("/efi"),
                        QStringLiteral("/boot")}.contains(data.espRelative)) {
        return fail(error, i18nd("miryu-toolkit", "The ESP must be a vfat EFI-GUID partition distinct from the root partition."));
    }
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.+-]{0,79}$"));
    if (!identifier.match(data.bootloaderId).hasMatch())
        return fail(error, i18nd("miryu-toolkit", "The bootloader ID must be 1 to 80 safe ASCII characters."));
    data.grubArchitecture = BootProbe::grubTarget(data.architecture);
    if (data.architecture.isEmpty() || data.grubArchitecture.isEmpty()
        || (!snapshot.architecture.isEmpty() && snapshot.architecture != data.architecture)) {
        return fail(error, i18nd("miryu-toolkit", "The requested GRUB EFI architecture does not match this host."));
    }
    if (data.registerEfi && (!snapshot.uefi || executable(QStringLiteral("efibootmgr")).isEmpty()))
        return fail(error, i18nd("miryu-toolkit", "UEFI firmware registration is unavailable."));
    if (data.registerEfi) {
        for (const auto &entry : snapshot.firmware) {
            if (entry.label == data.bootloaderId) {
                return fail(error, i18nd("miryu-toolkit",
                    "A UEFI boot entry named %1 already exists. Remove it or use a different bootloader ID.",
                    data.bootloaderId));
            }
        }
    }

    if (!data.espPath.isEmpty()) {
        if (QDir::cleanPath(data.espPath) != data.espPath || !noSymlinks(data.espPath))
            return fail(error, i18nd("miryu-toolkit", "The ESP path is not canonical or contains a symbolic link."));
        const QString canonical = QFileInfo(data.espPath).canonicalFilePath();
        for (const auto &mount : snapshot.espMounts) {
            if (QFileInfo(mount.path).canonicalFilePath() == canonical
                && mount.source == data.espDevice) {
                data.existingEspMount = canonical;
                data.espPath = canonical;
                break;
            }
        }
        if (data.existingEspMount.isEmpty())
            return fail(error, i18nd("miryu-toolkit", "The selected ESP mount is not active or its source changed."));
    }
    if (data.target.automatic) {
        if (executable(QStringLiteral("mount")).isEmpty()
            || executable(QStringLiteral("umount")).isEmpty()) {
            return fail(error, i18nd("miryu-toolkit", "mount and umount are required for automatic repair."));
        }
    } else {
        const QString root = QFileInfo(data.target.root).canonicalFilePath();
        const QString expectedEsp = rootPath(root, data.espRelative);
        if (data.espPath.isEmpty() || QFileInfo(expectedEsp).canonicalFilePath() != data.espPath)
            return fail(error, i18nd("miryu-toolkit", "The mounted ESP must be at the selected path inside the target root."));
        if (!checkSeparateBoot(root, data.espRelative, error))
            return false;
    }
    *result = data;
    return true;
}

class RepairMounts {
public:
    ~RepairMounts()
    {
        QString ignored;
        cleanup(&ignored);
    }

    bool prepareRoot(const RepairTarget &target, QString *error)
    {
        if (!target.automatic) {
            root_ = QFileInfo(target.root).canonicalFilePath();
            return !root_.isEmpty() && validLinuxRoot(root_)
                ? true : fail(error, i18nd("miryu-toolkit", "The mounted target is not a valid Linux root."));
        }
        temporary_ = std::make_unique<QTemporaryDir>(QStringLiteral("/run/miryu-toolkit-repair-XXXXXX"));
        if (!temporary_->isValid())
            return fail(error, i18nd("miryu-toolkit", "Cannot create a private repair mount directory."));
        temporary_->setAutoRemove(false);
        QFile::setPermissions(temporary_->path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        root_ = temporary_->path();
        QStringList arguments;
        if (!target.subvolume.isEmpty())
            arguments << QStringLiteral("-o") << QStringLiteral("rw,subvol=") + target.subvolume;
        arguments << target.rootDevice << root_;
        if (!mount(arguments, root_, false, error))
            return false;
        if (!runCommand(executable(QStringLiteral("mount")),
                        {QStringLiteral("--make-rprivate"), root_}, 30000, error))
            return false;
        if (mountSource(root_) != target.rootDevice || !validLinuxRoot(root_))
            return fail(error, i18nd("miryu-toolkit", "The selected root or subvolume is not the expected Linux system."));
        return true;
    }

    bool prepareEsp(const RepairRequest &request, QString *error)
    {
        if (!checkSeparateBoot(root_, request.espRelative, error))
            return false;
        esp_ = rootPath(root_, request.espRelative);
        if (!request.target.automatic)
            return mountPoint(root_, request.espRelative, false, error);
        if (!mountPoint(root_, request.espRelative, true, error))
            return false;
        const QStringList arguments = !request.existingEspMount.isEmpty()
            ? QStringList{QStringLiteral("--bind"), request.existingEspMount, esp_}
            : QStringList{request.espDevice, esp_};
        return mount(arguments, esp_, false, error);
    }

    bool prepareChroot(QString *error)
    {
        if (root_ == QLatin1String("/"))
            return true;
        if (executable(QStringLiteral("chroot")).isEmpty()
            || executable(QStringLiteral("mount")).isEmpty()
            || executable(QStringLiteral("umount")).isEmpty())
            return fail(error, i18nd("miryu-toolkit", "chroot, mount and umount are required to repair another Linux root."));
        for (const auto &name : {QStringLiteral("/dev"), QStringLiteral("/sys"), QStringLiteral("/run")}) {
            const QString destination = rootPath(root_, name);
            if (!mountPoint(root_, name, true, error))
                return false;
            if (!mountSource(destination).isEmpty())
                continue;
            if (!mount({QStringLiteral("--rbind"), name, destination}, destination, true, error))
                return false;
            if (!runCommand(executable(QStringLiteral("mount")),
                            {QStringLiteral("--make-rprivate"), destination}, 30000, error)) {
                return false;
            }
        }
        const QString proc = rootPath(root_, QStringLiteral("/proc"));
        if (!mountPoint(root_, QStringLiteral("/proc"), true, error))
            return false;
        if (mountSource(proc).isEmpty()
            && !mount({QStringLiteral("-t"), QStringLiteral("proc"), QStringLiteral("proc"), proc},
                      proc, false, error)) {
            return false;
        }
        return true;
    }

    bool cleanup(QString *error)
    {
        bool success = true;
        QList<Mount> remaining;
        for (auto iterator = mounts_.crbegin(); iterator != mounts_.crend(); ++iterator) {
            QStringList arguments;
            if (iterator->recursive)
                arguments.append(QStringLiteral("-R"));
            arguments.append(iterator->path);
            QString cleanupError;
            if (!runCommand(executable(QStringLiteral("umount")), arguments, 30000, &cleanupError)) {
                output(i18nd("miryu-toolkit", "A repair mount remains at %1", iterator->path));
                remaining.prepend(*iterator);
                success = false;
            }
        }
        mounts_ = remaining;
        if (success && temporary_) {
            // Never recursively remove this directory: a failed unmount must not
            // turn temporary-directory cleanup into deletion of a mounted OS.
            if (QDir().rmdir(temporary_->path()))
                temporary_.reset();
            else
                output(i18nd("miryu-toolkit", "Temporary repair directory retained: %1", temporary_->path()));
        }
        if (!success)
            return fail(error, i18nd("miryu-toolkit", "One or more temporary repair mounts could not be unmounted."));
        return true;
    }

    QString root() const { return root_; }
    QString esp() const { return esp_; }

private:
    struct Mount {
        QString path;
        bool recursive = false;
    };

    bool mount(const QStringList &arguments, const QString &path, bool recursive, QString *error)
    {
        mounts_.append({path, recursive});
        return runCommand(executable(QStringLiteral("mount")), arguments, 60000, error);
    }

    std::unique_ptr<QTemporaryDir> temporary_;
    QString root_;
    QString esp_;
    QList<Mount> mounts_;
};

bool finishBackup(BootBackup *backup, const QString &action, bool success, QString *error)
{
    QString recordError;
    if (!backup->record(action, success, &recordError)) {
        fail(error, recordError);
        backup->finish(false, &recordError);
        return false;
    }
    QString finishError;
    if (!backup->finish(success, &finishError)) {
        fail(error, finishError);
        return false;
    }
    return success;
}

QStringList stringList(const QJsonValue &value, bool *ok)
{
    *ok = true;
    if (value.isArray()) {
        QStringList result;
        for (const auto &item : value.toArray()) {
            if (!item.isString()) {
                *ok = false;
                return {};
            }
            result.append(item.toString());
        }
        return result;
    }
    if (value.isString())
        return value.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    *ok = false;
    return {};
}

QString normalizedNumber(const QString &number)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[0-9A-Fa-f]{4}\\z"));
    return pattern.match(number).hasMatch() ? number.toUpper() : QString();
}

bool expectedIdentity(const QJsonObject &request, const FirmwareEntry *entry)
{
    const QString expected = request.contains(QStringLiteral("expectedIdentity"))
        ? request.value(QStringLiteral("expectedIdentity")).toString()
        : request.value(QStringLiteral("expected")).toString();
    return entry && !expected.isEmpty() && expected == entry->identity();
}

bool expectedOrder(const QJsonObject &request, const BootSnapshot &snapshot)
{
    if (!request.contains(QStringLiteral("expectedOrder")))
        return false;
    bool ok = false;
    const QStringList expected = stringList(request.value(QStringLiteral("expectedOrder")), &ok);
    return ok && expected == snapshot.bootOrder;
}

bool expectedFirmware(const QJsonObject &request, const BootSnapshot &snapshot)
{
    if (!request.contains(QStringLiteral("expectedFirmware")))
        return true;
    bool ok = false;
    QStringList expected = stringList(request.value(QStringLiteral("expectedFirmware")), &ok);
    if (!ok)
        return false;
    QStringList current;
    for (const auto &entry : snapshot.firmware)
        current.append(entry.identity());
    expected.sort();
    current.sort();
    return expected == current;
}

bool efiOrderArguments(const QJsonObject &request, const BootSnapshot &snapshot,
                       QStringList *arguments, QString *error)
{
    if (!expectedOrder(request, snapshot) || !request.contains(QStringLiteral("order")))
        return fail(error, i18nd("miryu-toolkit", "The firmware boot order changed since it was reviewed."));
    bool ok = false;
    const QStringList order = stringList(request.value(QStringLiteral("order")), &ok);
    QSet<QString> requested;
    QSet<QString> current;
    for (const auto &raw : snapshot.bootOrder) {
        const QString number = normalizedNumber(raw);
        if (number.isEmpty() || current.contains(number) || !snapshot.entry(number))
            return fail(error, i18nd("miryu-toolkit", "The current firmware BootOrder is invalid."));
        current.insert(number);
    }
    for (const auto &raw : order) {
        const QString number = normalizedNumber(raw);
        if (number.isEmpty() || requested.contains(number) || !snapshot.entry(number))
            return fail(error, i18nd("miryu-toolkit", "Boot order numbers must be existing, unique four-digit firmware IDs."));
        requested.insert(number);
    }
    if (!ok || order.isEmpty() || requested != current || requested.size() != order.size())
        return fail(error, i18nd("miryu-toolkit", "The requested order must contain exactly the entries already in BootOrder."));
    *arguments = {QStringLiteral("-o"), order.join(QLatin1Char(','))};
    return true;
}

bool createEfiBackup(const BootSnapshot &snapshot, BootBackup *backup, QString *error)
{
    RepairTarget target;
    target.root = QStringLiteral("/");
    for (const auto &candidate : snapshot.repairTargets) {
        if (!candidate.automatic && candidate.root == QLatin1String("/")) {
            target = candidate;
            break;
        }
    }
    if (target.rootDevice.isEmpty())
        target.rootDevice = mountSource(QStringLiteral("/"));
    if (!backup->create(snapshot, target, QStringLiteral("/"), {},
                        BootBackup::defaultRoot(), error)) {
        return false;
    }
    output(i18nd("miryu-toolkit", "EFI state backup: %1", backup->directory()));
    return true;
}

bool firmwareChangeApplied(const QString &number, const QString &label, int enabled,
                           QString *observed)
{
    const QString path = BootProbe::bootVariablePath(number);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    BootProbe::BootVariable variable;
    if (!BootProbe::readBootVariable(path, &variable, observed))
        return true;
    if (!label.isEmpty() && variable.description != label) {
        if (observed)
            *observed = variable.description;
        return false;
    }
    if (enabled >= 0 && variable.active() != (enabled == 1)) {
        if (observed)
            *observed = variable.active() ? QStringLiteral("1") : QStringLiteral("0");
        return false;
    }
    if (observed)
        observed->clear();
    return true;
}

bool confirmEfiChange(const QString &number, const QString &label, int enabled, QString *error)
{
    QString observed;
    if (firmwareChangeApplied(number, label, enabled, &observed))
        return true;

    // Some firmware silently ignores efibootmgr's -b modifications, so update the
    // Boot#### variable directly and verify the result.
    const QString path = BootProbe::bootVariablePath(number);
    QString directError;
    const bool applied = !label.isEmpty()
        ? BootProbe::rewriteBootVariableDescription(path, label, &directError)
        : BootProbe::setBootVariableActive(path, enabled == 1, &directError);
    if (applied) {
        observed.clear();
        if (firmwareChangeApplied(number, label, enabled, &observed)) {
            output(i18nd("miryu-toolkit", "Updated the EFI boot variable directly because the firmware ignored the efibootmgr change."));
            return true;
        }
    }

    const QString reason = applied ? observed : directError;
    return fail(error, i18nd("miryu-toolkit", "The firmware did not apply the change to boot entry %1. Current value: %2",
                            number, reason.isEmpty() ? i18nd("miryu-toolkit", "unchanged") : reason));
}

bool applyEfiStep(const QString &action, const QJsonObject &request, QString *error)
{
    const BootSnapshot snapshot = BootProbe::inspect();
    if (!snapshot.uefi || executable(QStringLiteral("efibootmgr")).isEmpty()
        || !expectedFirmware(request, snapshot)) {
        return fail(error, i18nd("miryu-toolkit", "UEFI firmware management is unavailable or its expected state changed."));
    }
    QString program = executable(QStringLiteral("efibootmgr"));
    QStringList arguments;
    QString renameLabel;
    QString toggleNumber;
    int toggleEnabled = -1;

    if (action == QLatin1String("efi-order")) {
        if (!efiOrderArguments(request, snapshot, &arguments, error))
            return false;
    } else if (action == QLatin1String("efi-next")) {
        if (!request.contains(QStringLiteral("expectedNext"))
            || request.value(QStringLiteral("expectedNext")).toString() != snapshot.bootNext) {
            return fail(error, i18nd("miryu-toolkit", "The next-boot state changed since it was reviewed."));
        }
        const QString raw = request.value(QStringLiteral("number")).toString();
        if (raw.isEmpty()) {
            arguments = {QStringLiteral("-N")};
        } else {
            const QString number = normalizedNumber(raw);
            if (number.isEmpty() || !snapshot.entry(number))
                return fail(error, i18nd("miryu-toolkit", "The requested next-boot entry does not exist."));
            arguments = {QStringLiteral("-n"), number};
        }
    } else if (action == QLatin1String("efi-toggle")
               || action == QLatin1String("efi-rename")
               || action == QLatin1String("efi-remove")) {
        const QString number = normalizedNumber(request.value(QStringLiteral("number")).toString());
        const FirmwareEntry *entry = snapshot.entry(number);
        if (!expectedIdentity(request, entry))
            return fail(error, i18nd("miryu-toolkit", "The firmware entry changed since it was reviewed."));
        if (action == QLatin1String("efi-toggle")) {
            const QJsonValue value = request.value(QStringLiteral("enabled"));
            int enabled = -1;
            if (value.isBool())
                enabled = value.toBool() ? 1 : 0;
            else if (value.isString() && (value.toString() == QLatin1String("0") || value.toString() == QLatin1String("1")))
                enabled = value.toString().toInt();
            else if (value.isDouble() && (value.toDouble(-1) == 0 || value.toDouble(-1) == 1))
                enabled = value.toInt();
            if (enabled < 0)
                return fail(error, i18nd("miryu-toolkit", "The enabled state must be 0 or 1."));
            arguments = {QStringLiteral("-b"), number,
                         enabled ? QStringLiteral("-a") : QStringLiteral("-A")};
            toggleNumber = number;
            toggleEnabled = enabled;
        } else if (action == QLatin1String("efi-rename")) {
            const QString rawLabel = request.value(QStringLiteral("label")).toString();
            const QString label = rawLabel.trimmed();
            bool valid = !rawLabel.isEmpty() && rawLabel.size() <= 80 && !label.isEmpty();
            for (const QChar character : rawLabel)
                valid = valid && character.isPrint();
            if (!valid)
                return fail(error, i18nd("miryu-toolkit", "The firmware label must be 1 to 80 printable characters."));
            arguments = {QStringLiteral("-b"), number, QStringLiteral("-L"), label};
            renameLabel = label;
        } else {
            if (entry->current || number == snapshot.bootCurrent)
                return fail(error, i18nd("miryu-toolkit", "BootCurrent cannot be removed."));
            arguments = {QStringLiteral("-b"), number, QStringLiteral("-B")};
        }
    } else if (action == QLatin1String("efi-add")) {
        if (!expectedOrder(request, snapshot))
            return fail(error, i18nd("miryu-toolkit", "The firmware boot order changed since it was reviewed."));
        if (!BootProbe::firmwareAddArguments(request.value(QStringLiteral("path")).toString(),
                                             request.value(QStringLiteral("label")).toString(),
                                             snapshot.espMounts, &arguments, error)) {
            return false;
        }
        arguments.prepend(QStringLiteral("--create"));
    } else {
        return fail(error, i18nd("miryu-toolkit", "Unsupported EFI action."));
    }

    bool success = runCommand(program, arguments, 120000, error);
    if (success && (!renameLabel.isEmpty() || toggleEnabled >= 0)) {
        const QString number = toggleEnabled >= 0
            ? toggleNumber
            : normalizedNumber(request.value(QStringLiteral("number")).toString());
        success = confirmEfiChange(number, renameLabel, toggleEnabled, error);
    }
    if (success && action == QLatin1String("efi-order")) {
        const int option = arguments.indexOf(QStringLiteral("-o"));
        if (option >= 0 && option + 1 < arguments.size()) {
            const QStringList expected =
                arguments.at(option + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
            const BootSnapshot after = BootProbe::inspect();
            QStringList actual;
            for (const auto &entry : after.bootOrder)
                actual.append(entry.toUpper());
            QStringList wanted;
            for (const auto &entry : expected)
                wanted.append(entry.toUpper());
            if (actual != wanted) {
                success = false;
                *error = i18nd("miryu-toolkit", "The firmware did not accept the new boot order. Current order: %1",
                              after.bootOrder.isEmpty() ? i18nd("miryu-toolkit", "empty")
                                                        : after.bootOrder.join(QLatin1Char(',')));
            }
        }
    }
    if (success && action == QLatin1String("efi-next")) {
        const QString expected =
            normalizedNumber(request.value(QStringLiteral("number")).toString());
        const BootSnapshot after = BootProbe::inspect();
        if (after.bootNext.compare(expected, Qt::CaseInsensitive) != 0) {
            success = false;
            *error = i18nd("miryu-toolkit", "The firmware did not accept the next-boot entry. Current value: %1",
                          after.bootNext.isEmpty() ? i18nd("miryu-toolkit", "empty") : after.bootNext);
        }
    }
    return success;
}

bool validEfiStepAction(const QString &action)
{
    return action == QLatin1String("efi-order") || action == QLatin1String("efi-next")
        || action == QLatin1String("efi-toggle") || action == QLatin1String("efi-rename")
        || action == QLatin1String("efi-remove") || action == QLatin1String("efi-add");
}

bool applyEfiBatch(const QJsonObject &request, QString *error)
{
    const QJsonArray steps = request.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty() || steps.size() > 16)
        return fail(error, i18nd("miryu-toolkit", "The EFI change set is empty or too large."));
    const BootSnapshot snapshot = BootProbe::inspect();
    if (!snapshot.uefi || executable(QStringLiteral("efibootmgr")).isEmpty())
        return fail(error, i18nd("miryu-toolkit", "UEFI firmware management is unavailable."));

    BootBackup backup;
    if (!createEfiBackup(snapshot, &backup, error))
        return false;
    bool success = true;
    for (const auto &value : steps) {
        const QJsonObject step = value.toObject();
        const QString action = step.value(QStringLiteral("action")).toString();
        if (!validEfiStepAction(action)) {
            success = false;
            *error = i18nd("miryu-toolkit", "The EFI change set contains an unsupported action.");
            break;
        }
        if (!applyEfiStep(action, step.value(QStringLiteral("request")).toObject(), error)) {
            success = false;
            break;
        }
    }
    return finishBackup(&backup, QStringLiteral("efi-batch"), success, error);
}

bool applyEfi(const QString &action, const QJsonObject &request, QString *error)
{
    if (action == QLatin1String("efi-batch"))
        return applyEfiBatch(request, error);
    const BootSnapshot snapshot = BootProbe::inspect();
    BootBackup backup;
    if (!createEfiBackup(snapshot, &backup, error))
        return false;
    return finishBackup(&backup, action, applyEfiStep(action, request, error), error);
}

bool readCertificate(const QJsonObject &request, QByteArray *certificate, QString *error)
{
    const QByteArray encoded = request.value(QStringLiteral("certificate")).toString().toLatin1();
    const QString path = request.value(QStringLiteral("certificatePath")).toString();
    if (!encoded.isEmpty() && !path.isEmpty())
        return fail(error, i18nd("miryu-toolkit", "Specify either certificate base64 or certificatePath, not both."));
    if (!encoded.isEmpty()) {
        if (encoded.size() > 96 * 1024)
            return fail(error, i18nd("miryu-toolkit", "The encoded certificate is too large."));
        *certificate = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
    } else {
        const QFileInfo info(path);
        if (path.isEmpty() || info.isSymLink() || !info.isFile() || info.size() > 64 * 1024)
            return fail(error, i18nd("miryu-toolkit", "The certificate path is not a regular DER file of allowed size."));
        QFile file(info.canonicalFilePath());
        if (!file.open(QIODevice::ReadOnly))
            return fail(error, i18nd("miryu-toolkit", "Cannot read the DER certificate."));
        *certificate = file.read(64 * 1024 + 1);
    }
    return BootProbe::validateDerCertificate(*certificate, error);
}

bool applyMok(const QString &action, const QJsonObject &request, QString *error)
{
    if (!QDir(QStringLiteral("/sys/firmware/efi")).exists()
        || executable(QStringLiteral("mokutil")).isEmpty()) {
        return fail(error, i18nd("miryu-toolkit", "MOK management requires UEFI and mokutil."));
    }
    const bool revoke = action == QLatin1String("mok-revoke");
    QByteArray certificate;
    QString password;
    QTemporaryFile temporary(QStringLiteral("/run/miryu-toolkit-mok-XXXXXX.der"));
    QStringList arguments{QStringLiteral("--revoke-import")};
    if (!revoke) {
        if (!readCertificate(request, &certificate, error))
            return false;
        password = request.value(QStringLiteral("password")).toString();
        bool validPassword = !password.isEmpty() && password.size() <= 256 && password.toUtf8().size() <= 1024;
        for (const QChar character : password)
            validPassword = validPassword && character.isPrint();
        if (!validPassword)
            return fail(error, i18nd("miryu-toolkit", "The MOK password must contain 1 to 256 printable characters."));
        if (!temporary.open()
            || !temporary.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
            || temporary.write(certificate) != certificate.size() || !temporary.flush())
            return fail(error, i18nd("miryu-toolkit", "Cannot prepare the temporary DER certificate."));
        temporary.close();
        arguments = {QStringLiteral("--import"), temporary.fileName()};
    }

    output(revoke ? QStringLiteral("mokutil --revoke-import")
                  : QStringLiteral("mokutil --import <temporary-DER>"));
    QProcess process;
    process.setProcessEnvironment(commandEnvironment());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable(QStringLiteral("mokutil")), arguments);
    if (!process.waitForStarted(10000)) {
        BootProbe::stop(process);
        return fail(error, i18nd("miryu-toolkit", "Could not start mokutil."));
    }
    if (!revoke) {
        const QByteArray input = password.toUtf8() + '\n' + password.toUtf8() + '\n';
        if (process.write(input) != input.size()) {
            BootProbe::stop(process);
            return fail(error, i18nd("miryu-toolkit", "Could not provide the MOK password to mokutil."));
        }
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(60000)) {
        BootProbe::stop(process);
        return fail(error, i18nd("miryu-toolkit", "mokutil timed out."));
    }
    const QString commandOutput = sanitize(QString::fromUtf8(process.readAllStandardOutput()), password).trimmed();
    if (!commandOutput.isEmpty())
        output(commandOutput);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail(error, i18nd("miryu-toolkit", "mokutil did not complete the request."));
    return true;
}

bool runTargetCommand(const QString &root, const QString &program,
                      const QStringList &arguments, QString *error)
{
    if (root == QLatin1String("/"))
        return runCommand(targetExecutable(root, program), arguments, 600000, error);
    QStringList chrootArguments{root, program};
    chrootArguments.append(arguments);
    return runCommand(executable(QStringLiteral("chroot")), chrootArguments, 600000, error);
}

QString efiBinary(const QString &esp, const RepairRequest &request)
{
    const QString suffix = request.architecture == QLatin1String("x64") ? QStringLiteral("x64")
        : request.architecture == QLatin1String("aa64") ? QStringLiteral("aa64") : QStringLiteral("ia32");
    const QString directory = esp + QStringLiteral("/EFI/") + request.bootloaderId;
    QStringList candidates{directory + QStringLiteral("/shim") + suffix + QStringLiteral(".efi"),
                           directory + QStringLiteral("/grub") + suffix + QStringLiteral(".efi")};
    if (request.fallback) {
        const QString fallback = esp + QStringLiteral("/EFI/BOOT/BOOT") + suffix.toUpper() + QStringLiteral(".EFI");
        candidates.append(fallback);
    }
    for (const auto &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && !info.isSymLink())
            return info.canonicalFilePath();
    }
    return {};
}

QString efiArchSuffix(const QString &architecture)
{
    if (architecture == QLatin1String("x64"))
        return QStringLiteral("x64");
    if (architecture == QLatin1String("aa64"))
        return QStringLiteral("aa64");
    return QStringLiteral("ia32");
}

bool ensureSignedEfiBinaries(const QString &root, const QString &esp,
                             const RepairRequest &request, QString *error)
{
    const QString suffix = efiArchSuffix(request.architecture);
    const QString directory = esp + QStringLiteral("/EFI/") + request.bootloaderId;

    const auto present = [](const QString &path) {
        const QFileInfo info(path);
        return info.isFile() && !info.isSymLink() && info.size() > 0;
    };
    const auto inDir = [](const QString &dir, const QString &name) {
        return dir + QLatin1Char('/') + name;
    };

    const QString shimName = QStringLiteral("shim") + suffix + QStringLiteral(".efi");
    const QString grubName = QStringLiteral("grub") + suffix + QStringLiteral(".efi");
    const QString mmName = QStringLiteral("mm") + suffix + QStringLiteral(".efi");
    const QString cfgName = QStringLiteral("grub.cfg");

    // Fast path: everything is already in place.
    if (present(inDir(directory, shimName)) && present(inDir(directory, grubName))
        && present(inDir(directory, cfgName)))
        return true;

    // Gather candidate source directories: every vendor directory on the
    // target ESP, plus the host ESP and shim data directories when repairing
    // another root. Fedora ships signed shim/grub under /EFI/fedora even when
    // a different bootloader id was requested.
    QStringList searchDirs;
    QDir espEfi(esp + QStringLiteral("/EFI"));
    const auto targetVendors = espEfi.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &vendor : targetVendors)
        searchDirs.append(esp + QStringLiteral("/EFI/") + vendor);
    if (root != QLatin1String("/")) {
        static const QStringList hostEsps{QStringLiteral("/boot/efi"), QStringLiteral("/efi")};
        for (const auto &hostEsp : hostEsps) {
            QDir hostEfi(hostEsp + QStringLiteral("/EFI"));
            const auto hostVendors = hostEfi.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const auto &vendor : hostVendors)
                searchDirs.append(hostEsp + QStringLiteral("/EFI/") + vendor);
        }
        searchDirs.append(QStringLiteral("/usr/share/shim"));
    }

    // Copy shim and grub (mandatory), mm/MokManager (optional) and grub.cfg
    // (preferred from a vendor directory) into the requested bootloader id
    // directory. No packages are installed here; the repair only reuses
    // existing signed EFI binaries.
    const auto copyFromSearch = [&](const QString &name) -> bool {
        const QString dest = inDir(directory, name);
        if (present(dest))
            return true;
        for (const auto &srcDir : searchDirs) {
            const QString src = inDir(srcDir, name);
            if (present(src)) {
                QDir().mkpath(directory);
                if (QFile::exists(dest))
                    QFile::remove(dest);
                if (QFile::copy(src, dest))
                    return true;
            }
        }
        return false;
    };

    const bool haveShim = copyFromSearch(shimName);
    const bool haveGrub = copyFromSearch(grubName);
    copyFromSearch(mmName); // MokManager is optional.
    const bool haveCfg = copyFromSearch(cfgName);

    if (!haveShim || !haveGrub) {
        fail(error, i18nd("miryu-toolkit", "Signed EFI bootloader files (shim and grub) could not be found for %1.",
                         request.bootloaderId));
        return false;
    }

    // If no vendor grub.cfg was available, first try the official
    // gen_grub_cfgstub script from grub2-tools — the same helper Anaconda
    // invokes. It determines the boot filesystem UUID itself and writes the
    // redirecting stub to the requested EFI directory.
    if (!haveCfg) {
        const QString stubScript = targetExecutable(root, QStringLiteral("gen_grub_cfgstub"));
        if (!stubScript.isEmpty()) {
            const QString efiDirRelative = request.espRelative
                + QStringLiteral("/EFI/") + request.bootloaderId;
            if (runTargetCommand(root, QStringLiteral("gen_grub_cfgstub"),
                    {QStringLiteral("/boot/grub2"), efiDirRelative}, error)
                && present(inDir(directory, cfgName))) {
                return true;
            }
            // gen_grub_cfgstub failed or produced no file; clear the error
            // and fall through to the manual UUID-based stub generation.
            if (error)
                error->clear();
        }

        // Manual fallback: write the Fedora-style stub that redirects the
        // EFI loader to /boot/grub2/grub.cfg on the boot filesystem.
        const QString bootPath = rootPath(root, QStringLiteral("/boot"));
        const QString bootDev = mountSource(bootPath);
        const QString rootDev = mountSource(rootPath(root, QStringLiteral("/")));
        const bool bootSeparate = !bootDev.isEmpty() && bootDev != rootDev;
        const QString uuid = BootProbe::runReadOnly(QStringLiteral("findmnt"),
            {QStringLiteral("-rn"), QStringLiteral("-M"), bootSeparate ? bootPath : rootPath(root, QStringLiteral("/")),
             QStringLiteral("-o"), QStringLiteral("UUID")}).trimmed();
        if (uuid.isEmpty()) {
            fail(error, i18nd("miryu-toolkit", "Could not determine the boot filesystem UUID for the EFI grub.cfg stub."));
            return false;
        }
        const QString prefix = bootSeparate
            ? QStringLiteral("($dev)/grub2")
            : QStringLiteral("($dev)/boot/grub2");
        const QString stub = QStringLiteral("search --no-floppy --fs-uuid --set=dev %1\n"
                                             "set prefix=%2\n"
                                             "export $prefix\n"
                                             "configfile $prefix/grub.cfg\n")
                                 .arg(uuid, prefix);
        QDir().mkpath(directory);
        QFile cfg(inDir(directory, cfgName));
        if (!cfg.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
            || cfg.write(stub.toUtf8()) != stub.toUtf8().size()) {
            fail(error, i18nd("miryu-toolkit", "Could not write the EFI grub.cfg stub for %1.", request.bootloaderId));
            return false;
        }
    }
    return true;
}

bool applyRepair(const QJsonObject &request, QString *error)
{
    const BootSnapshot snapshot = BootProbe::inspect();
    RepairRequest repair;
    if (!parseRepair(request, snapshot, &repair, error))
        return false;
    const bool bios = repair.mode == QLatin1String("bios");

    RepairMounts mounts;
    if (!mounts.prepareRoot(repair.target, error))
        return false;
    const QString root = mounts.root();
    if (bios) {
        if (!checkSeparateBoot(root, QString(), error) || !mounts.prepareChroot(error)) {
            mounts.cleanup(error);
            return false;
        }
    } else if (!mounts.prepareEsp(repair, error) || !mounts.prepareChroot(error)) {
        mounts.cleanup(error);
        return false;
    }
    if (!bios && mountSource(mounts.esp()) != repair.espDevice) {
        fail(error, i18nd("miryu-toolkit", "The ESP mount source changed before repair was applied."));
        mounts.cleanup(error);
        return false;
    }
    const bool currentSystem = (root == QLatin1String("/"));
    // UEFI repair always installs the signed shim + grub EFI binaries and
    // registers a firmware entry pointing at shim<arch>.efi, regardless of the
    // Secure Boot state. grub2-install for EFI produces unsigned images that
    // cannot boot under Secure Boot and is rejected outright by Fedora.

    if (targetExecutable(root, QStringLiteral("grub2-mkconfig")).isEmpty()) {
        fail(error, i18nd("miryu-toolkit", "The target system is missing grub2-mkconfig."));
        mounts.cleanup(error);
        return false;
    }
    const QString grubInstall = currentSystem
        ? targetExecutable(root, QStringLiteral("grub2-install"))
        : executable(QStringLiteral("grub2-install"));
    if (bios && grubInstall.isEmpty()) {
        fail(error, i18nd("miryu-toolkit", "GRUB repair requires grub2-install on the running system."));
        mounts.cleanup(error);
        return false;
    }
    if (bios) {
        const QString moduleRoot = currentSystem ? root : QStringLiteral("/");
        if (findGrubModuleDirectory(moduleRoot, repair.grubArchitecture).isEmpty()) {
            fail(error, i18nd("miryu-toolkit", "No GRUB modules for %1 were found. Install the matching GRUB module package and try again.",
                             repair.grubArchitecture));
            mounts.cleanup(error);
            return false;
        }
    }

    BootBackup backup;
    if (!backup.create(snapshot, repair.target, root, BootBackup::configurationPaths(root),
                       BootBackup::defaultRoot(), error)) {
        mounts.cleanup(error);
        return false;
    }
    output(i18nd("miryu-toolkit", "Repair configuration backup: %1", backup.directory()));

    const QString bootDirectory = rootPath(root, QStringLiteral("/boot/grub2"));
    bool success = true;
    if (bios) {
        QStringList installArguments{QStringLiteral("--target=") + repair.grubArchitecture,
                                     QStringLiteral("--recheck"), repair.disk};
        if (!currentSystem)
            installArguments.prepend(QStringLiteral("--boot-directory=") + bootDirectory);
        success = runCommand(grubInstall, installArguments, 600000, error);
    } else {
        // UEFI always uses the signed shim + grub binaries so the firmware
        // entry registered below points at shim<arch>.efi. grub2-install for
        // EFI is intentionally not used: it produces unsigned images that
        // cannot boot under Secure Boot and Fedora's patched binary refuses
        // EFI outright.
        success = ensureSignedEfiBinaries(root, mounts.esp(), repair, error);
    }
    if (!backup.record(QStringLiteral("grub2-install"), success, error)) {
        mounts.cleanup(error);
        backup.finish(false, error);
        return false;
    }
    if (success)
        success = runTargetCommand(root, QStringLiteral("grub2-mkconfig"),
            {QStringLiteral("-o"), QStringLiteral("/boot/grub2/grub.cfg")}, error);
    if (!backup.record(QStringLiteral("grub2-mkconfig"), success, error)) {
        mounts.cleanup(error);
        backup.finish(false, error);
        return false;
    }

    // Verify that the generated configuration files actually exist and carry
    // real boot entries. grub2-mkconfig can exit 0 while producing an empty
    // file (e.g. no kernels installed), which would leave the system at the
    // GRUB shell after reboot.
    if (success) {
        const QString mainCfg = rootPath(root, QStringLiteral("/boot/grub2/grub.cfg"));
        const QFileInfo mainInfo(mainCfg);
        if (!mainInfo.isFile() || mainInfo.size() == 0) {
            success = fail(error, i18nd("miryu-toolkit",
                "grub2-mkconfig did not produce %1.", QStringLiteral("/boot/grub2/grub.cfg")));
        } else {
            QFile cfg(mainCfg);
            if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString content = QString::fromUtf8(cfg.readAll());
                if (!content.contains(QStringLiteral("menuentry"))
                    && !content.contains(QStringLiteral("blscfg"))) {
                    success = fail(error, i18nd("miryu-toolkit",
                        "The generated %1 contains no boot entries.",
                        QStringLiteral("/boot/grub2/grub.cfg")));
                }
            }
        }
        if (!bios && success) {
            const QString efiCfg = mounts.esp() + QStringLiteral("/EFI/")
                + repair.bootloaderId + QStringLiteral("/grub.cfg");
            const QFileInfo efiInfo(efiCfg);
            if (!efiInfo.isFile() || efiInfo.size() == 0) {
                success = fail(error, i18nd("miryu-toolkit",
                    "The EFI bootloader directory is missing %1.",
                    QStringLiteral("grub.cfg")));
            }
        }
        if (!success && !backup.record(QStringLiteral("grub-cfg-verify"), success, error)) {
            mounts.cleanup(error);
            backup.finish(false, error);
            return false;
        }
    }

    // The UEFI install was deliberately run with --no-nvram. Registration is the
    // final write, after both the EFI image and generated configuration exist.
    if (success && repair.registerEfi && !bios) {
        const QString binary = efiBinary(mounts.esp(), repair);
        QStringList firmwareArguments;
        success = !binary.isEmpty()
            && BootProbe::firmwareAddArguments(binary, repair.bootloaderId, &firmwareArguments, error);
        if (!success && error && error->isEmpty())
            fail(error, i18nd("miryu-toolkit", "The installed GRUB EFI binary could not be found for registration."));
        if (success) {
            firmwareArguments.prepend(QStringLiteral("--create"));
            success = runCommand(executable(QStringLiteral("efibootmgr")), firmwareArguments, 120000, error);
        }
        if (!backup.record(QStringLiteral("firmware-register"), success, error)) {
            mounts.cleanup(error);
            backup.finish(false, error);
            return false;
        }
    }
    QString cleanupError;
    if (!mounts.cleanup(&cleanupError)) {
        success = false;
        fail(error, cleanupError);
    }
    return finishBackup(&backup, QStringLiteral("repair"), success, error);
}

bool applyRestore(const QJsonObject &request, QString *error)
{
    const QString id = request.value(QStringLiteral("backupId")).toString();
    const QString digest = request.value(QStringLiteral("digest")).toString();
    const RepairTarget target = requestTarget(request);
    const BootSnapshot snapshot = BootProbe::inspect();
    if (!validateTarget(target, snapshot, error))
        return false;

    RepairMounts mounts;
    if (!mounts.prepareRoot(target, error))
        return false;
    const QString root = mounts.root();
    if (!BootBackup::validateRestore(BootBackup::defaultRoot(), id, digest, target, root, error)) {
        mounts.cleanup(error);
        return false;
    }
    QString pathsError;
    const QStringList paths = BootBackup::restorePaths(BootBackup::defaultRoot(), id, &pathsError);
    if (!pathsError.isEmpty()) {
        fail(error, pathsError);
        mounts.cleanup(error);
        return false;
    }

    BootBackup current;
    if (!current.create(snapshot, target, root, paths, BootBackup::defaultRoot(), error)) {
        mounts.cleanup(error);
        return false;
    }
    output(i18nd("miryu-toolkit", "Pre-restore configuration backup: %1", current.directory()));
    bool success = BootBackup::restore(BootBackup::defaultRoot(), id, digest, target, root, error);
    if (!current.record(QStringLiteral("restore:") + id, success, error)) {
        mounts.cleanup(error);
        current.finish(false, error);
        return false;
    }
    QString cleanupError;
    if (!mounts.cleanup(&cleanupError)) {
        success = false;
        fail(error, cleanupError);
    }
    if (!current.finish(success, error))
        return false;
    if (!success)
        return false;
    output(i18nd("miryu-toolkit", "Configuration originals restored. Generated grub.cfg, EFI binaries and NVRAM were not restored; regenerate GRUB configuration when required."));
    return true;
}

bool readRequest(const QString &path, bool fromStdin, QByteArray *data, QString *error)
{
    QFile input;
    if (fromStdin) {
        if (!input.open(stdin, QIODevice::ReadOnly))
            return fail(error, i18nd("miryu-toolkit", "Cannot read the request from standard input."));
    } else {
        input.setFileName(path);
        const QFileInfo info(path);
        if (!info.isFile() || info.size() > maximumRequestBytes || !input.open(QIODevice::ReadOnly))
            return fail(error, i18nd("miryu-toolkit", "The request file is unavailable or exceeds 256 KiB."));
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(8192);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError)
            return fail(error, i18nd("miryu-toolkit", "Could not read the request."));
        data->append(chunk);
        if (data->size() > maximumRequestBytes)
            return fail(error, i18nd("miryu-toolkit", "The request exceeds 256 KiB."));
    }
    return true;
}

} // namespace

#ifndef MIRYU_BOOT_HELPER_TEST
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("miryu-toolkit-boot-helper"));
    KLocalizedString::setApplicationDomain("miryu-toolkit");
    const QStringList arguments = application.arguments();
    if (arguments.size() == 5 && arguments.at(3) == QLatin1String("--lang")) {
        const QLocale locale(arguments.at(4));
        if (locale.language() != QLocale::C)
            KLocalizedString::setLanguages({locale.name()});
    }
    if (::geteuid() != 0) {
        QTextStream(stderr) << i18nd("miryu-toolkit", "Administrator authorization is required.") << Qt::endl;
        return 2;
    }
    // Probes and writers must not inherit a caller-controlled executable path.
    qputenv("PATH", QByteArray("/usr/sbin:/usr/bin:/sbin:/bin"));

#ifdef MIRYU_MOK_LIST_HELPER
    // This executable is implicitly authorized by polkit, so it must never
    // expose the write-capable boot operations from the regular helper.
    if (arguments.size() != 2 || arguments.at(1) != QLatin1String("--list-mok")) {
        QTextStream(stderr) << i18nd("miryu-toolkit", "Usage: miryu-toolkit-list-mok --list-mok") << Qt::endl;
        return 2;
    }
#endif

    // Silent read-only mode used by the unprivileged scan to obtain the pending
    // MOK list (mokutil --list-new requires root). Invoked through the
    // org.miryugaming.toolkit.list-mok polkit action which is allowed for
    // active sessions without authentication.
    if (arguments.size() >= 2 && arguments.at(1) == QLatin1String("--list-mok")) {
        const QString mokutil = executable(QStringLiteral("mokutil"));
        if (mokutil.isEmpty()) {
            QTextStream(stderr) << i18nd("miryu-toolkit", "mokutil is not installed.") << Qt::endl;
            return 2;
        }
        QProcess process;
        process.setProcessEnvironment(commandEnvironment());
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        process.start(mokutil, {QStringLiteral("--list-new")});
        if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
            BootProbe::stop(process);
            return 2;
        }
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 2;
    }

    QString action;
    QString requestPath;
    bool fromStdin = false;
    const QString usage = i18nd("miryu-toolkit",
        "Usage: boothelper --list-mok | --apply <action> <request-json-file> | --apply-stdin <action> [--lang <locale>]");
    if (arguments.size() == 4 && arguments.at(1) == QLatin1String("--apply")) {
        action = arguments.at(2);
        requestPath = arguments.at(3);
    } else if ((arguments.size() == 3 || arguments.size() == 5)
               && arguments.at(1) == QLatin1String("--apply-stdin")) {
        action = arguments.at(2);
        fromStdin = arguments.size() == 3
            || (arguments.at(3) == QLatin1String("--lang") && !arguments.at(4).isEmpty());
        if (!fromStdin) {
            QTextStream(stderr) << usage << Qt::endl;
            return 2;
        }
    } else {
        QTextStream(stderr) << usage << Qt::endl;
        return 2;
    }
    static const QSet<QString> actions{QStringLiteral("efi-order"), QStringLiteral("efi-next"),
        QStringLiteral("efi-toggle"), QStringLiteral("efi-rename"), QStringLiteral("efi-remove"),
        QStringLiteral("efi-add"), QStringLiteral("efi-batch"), QStringLiteral("mok-import"),
        QStringLiteral("mok-revoke"),
        QStringLiteral("repair"), QStringLiteral("restore")};
    if (!actions.contains(action)) {
        QTextStream(stderr) << i18nd("miryu-toolkit", "Unsupported privileged action.") << Qt::endl;
        return 2;
    }

    QString error;
    QByteArray payload;
    if (!readRequest(requestPath, fromStdin, &payload, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 3;
    }
    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QTextStream(stderr) << i18nd("miryu-toolkit", "The request is not a valid JSON object.") << Qt::endl;
        return 3;
    }
    const QJsonObject request = document.object();
    bool success = false;
    if (action.startsWith(QLatin1String("efi-")))
        success = applyEfi(action, request, &error);
    else if (action.startsWith(QLatin1String("mok-")))
        success = applyMok(action, request, &error);
    else if (action == QLatin1String("repair"))
        success = applyRepair(request, &error);
    else if (action == QLatin1String("restore"))
        success = applyRestore(request, &error);

    if (!success) {
        const QString password = request.value(QStringLiteral("password")).toString();
        QTextStream(stderr) << sanitize(error.isEmpty() ? i18nd("miryu-toolkit", "The privileged operation failed.") : error,
                                        password) << Qt::endl;
        return 4;
    }
    output(i18nd("miryu-toolkit", "Privileged operation completed."));
    return 0;
}
#endif
