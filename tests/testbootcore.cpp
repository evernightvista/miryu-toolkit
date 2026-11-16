#include "bootbackup.h"
#include "bootdata.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeAll(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(data) == data.size();
}

QString sha256Hex(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Minimal, well-formed DER SEQUENCE with a 64-byte body.
QByteArray validDer()
{
    QByteArray body(64, '\0');
    QByteArray der;
    der.append(char(0x30));
    der.append(char(body.size()));
    der.append(body);
    return der;
}

QByteArray efibootmgrSample()
{
    return QByteArrayLiteral(
        "BootCurrent: 0001\n"
        "Timeout: 1 seconds\n"
        "BootOrder: 0001,0000,0003\n"
        "BootNext: 0000\n"
        "Boot0001* Fedora\tHD(1,GPT,3f1a2b4c-5d6e-7f80-9a1b-2c3d4e5f6071,0x800,0x100000)/File(\\EFI\\fedora\\shimx64.efi)\n"
        "Boot0000* Windows Boot Manager\tHD(1,GPT,3f1a2b4c-5d6e-7f80-9a1b-2c3d4e5f6071,0x800,0x100000)/File(\\EFI\\Microsoft\\Boot\\bootmgfw.efi)\n"
        "Boot0003  Disabled Entry\tHD(2,GPT,aa11bb22-cc33-dd44-ee55-ff6677889900,0x100000,0x100000)/File(\\EFI\\other\\grubx64.efi)\n");
}

QByteArray mokutilSample()
{
    return QByteArrayLiteral(
        "[key 1]\n"
        "SHA1 Fingerprint: 11:22:33:44:55:66:77:88:99:00:aa:bb:cc:dd:ee:ff:00:11:22:33\n"
        "Subject:     C=US, ST=CA, L=Test, O=Miryu, OU=Boot, CN=Miryu Test CA\n"
        "Issuer:      C=US, ST=CA, L=Test, O=Miryu, OU=Boot, CN=Miryu Test CA\n"
        "Not After :  Nov 15 00:00:00 2030 GMT\n"
        "\n"
        "[key 2]\n"
        "SHA1 Fingerprint: aa:bb:cc:dd:ee:ff:00:11:22:33:44:55:66:77:88:99:00:11:22:44\n"
        "Subject:     CN=Second Key\n"
        "Issuer:      CN=Second Issuer\n"
        "Not After :  Jan  1 00:00:00 2031 GMT\n");
}

struct Fixture {
    QTemporaryDir root;
    QString source;
    QString backups;
    QString grubPath;
    QString dropinPath;
    QString entryPath;

    Fixture()
    {
        source = root.path() + QStringLiteral("/target");
        backups = root.path() + QStringLiteral("/backups");
        grubPath = source + QStringLiteral("/etc/default/grub");
        dropinPath = source + QStringLiteral("/etc/default/grub.d/99_miryu_boot.cfg");
        entryPath = source + QStringLiteral("/boot/loader/entries/fedora.conf");
        writeAll(grubPath, QByteArrayLiteral("GRUB_TIMEOUT=5\n"));
        writeAll(entryPath, QByteArrayLiteral("title Fedora\n"));
        QDir().mkpath(source + QStringLiteral("/etc/default/grub.d"));
    }

    RepairTarget target() const
    {
        RepairTarget value;
        value.root = QFileInfo(source).canonicalFilePath();
        value.rootDevice = QStringLiteral("/dev/miryu-test0");
        value.automatic = false;
        return value;
    }
};

} // namespace

class BootCoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parseEfiTable();
    void parseMokCertificates();
    void derValidation();
    void subvolumeValidation();
    void firmwareAddArgumentsRejectsUnsafePaths();
    void deviceFingerprintChanges();
    void grubArchitecture();
    void backupCreateAndRestore();
    void backupRejectsTamperedManifest();
    void backupRejectsTamperedOriginal();
    void backupRejectsOtherTarget();
    void backupRejectsDisallowedPath();
    void restoreRefusesModifiedCreatedFile();
    void restoreRemovesUnmodifiedCreatedFile();
    void efiBootVariable();
    void firmwareSecurityVariables();
};

void BootCoreTest::parseEfiTable()
{
    const BootSnapshot snapshot = BootProbe::parseEfiBootManager(QString::fromLatin1(efibootmgrSample()));
    QCOMPARE(snapshot.bootCurrent, QStringLiteral("0001"));
    QCOMPARE(snapshot.bootNext, QStringLiteral("0000"));
    QCOMPARE(snapshot.bootOrder, (QStringList{QStringLiteral("0001"), QStringLiteral("0000"),
                                              QStringLiteral("0003")}));
    QCOMPARE(snapshot.firmware.size(), 3);

    const FirmwareEntry *fedora = snapshot.entry(QStringLiteral("0001"));
    QVERIFY(fedora);
    QCOMPARE(fedora->label, QStringLiteral("Fedora"));
    QCOMPARE(fedora->path, QStringLiteral("\\EFI\\fedora\\shimx64.efi"));
    QCOMPARE(fedora->partitionUuid, QStringLiteral("3f1a2b4c-5d6e-7f80-9a1b-2c3d4e5f6071"));
    QVERIFY(fedora->enabled);
    QVERIFY(fedora->current);

    const FirmwareEntry *windows = snapshot.entry(QStringLiteral("0000"));
    QVERIFY(windows);
    QCOMPARE(windows->label, QStringLiteral("Windows Boot Manager"));
    QVERIFY(windows->enabled);
    QVERIFY(!windows->current);

    const FirmwareEntry *disabled = snapshot.entry(QStringLiteral("0003"));
    QVERIFY(disabled);
    QCOMPARE(disabled->label, QStringLiteral("Disabled Entry"));
    QVERIFY(!disabled->enabled);
    QCOMPARE(disabled->partitionUuid, QStringLiteral("aa11bb22-cc33-dd44-ee55-ff6677889900"));

    QCOMPARE(snapshot.bootOrder.contains(QStringLiteral("0002")), false);
}

void BootCoreTest::parseMokCertificates()
{
    const QList<MokCertificate> certificates =
        BootProbe::parseMokCertificates(QString::fromLatin1(mokutilSample()));
    QCOMPARE(certificates.size(), 2);
    QCOMPARE(certificates.at(0).commonName, QStringLiteral("Miryu Test CA"));
    QCOMPARE(certificates.at(0).issuer, QStringLiteral("C=US, ST=CA, L=Test, O=Miryu, OU=Boot, CN=Miryu Test CA"));
    QCOMPARE(certificates.at(0).notAfter, QStringLiteral("Nov 15 00:00:00 2030 GMT"));
    QCOMPARE(certificates.at(0).fingerprint,
             QStringLiteral("11:22:33:44:55:66:77:88:99:00:aa:bb:cc:dd:ee:ff:00:11:22:33"));
    QCOMPARE(certificates.at(1).commonName, QStringLiteral("Second Key"));

    QVERIFY(BootProbe::parseMokCertificates(QString()).isEmpty());
}

void BootCoreTest::derValidation()
{
    QString error;
    QVERIFY(BootProbe::validateDerCertificate(validDer(), &error));
    QVERIFY(error.isEmpty());

    QVERIFY(!BootProbe::validateDerCertificate(QByteArray(63, '\0'), &error));
    QVERIFY(!error.isEmpty());

    QByteArray trailing = validDer();
    trailing.append('\0');
    QVERIFY(!BootProbe::validateDerCertificate(trailing, &error));

    QByteArray wrongTag = validDer();
    wrongTag[0] = char(0x31);
    QVERIFY(!BootProbe::validateDerCertificate(wrongTag, &error));

    QVERIFY(!BootProbe::validateDerCertificate(QByteArray(64 * 1024 + 1, '\0'), &error));
}

void BootCoreTest::subvolumeValidation()
{
    QVERIFY(BootProbe::validSubvolume(QString()));
    QVERIFY(BootProbe::validSubvolume(QStringLiteral("root")));
    QVERIFY(BootProbe::validSubvolume(QStringLiteral("@fedora/root")));
    QVERIFY(!BootProbe::validSubvolume(QStringLiteral("/root")));
    QVERIFY(!BootProbe::validSubvolume(QStringLiteral("../root")));
    QVERIFY(!BootProbe::validSubvolume(QStringLiteral("root/../etc")));
    QVERIFY(!BootProbe::validSubvolume(QStringLiteral("root;rm")));
    QVERIFY(!BootProbe::validSubvolume(QString(201, QLatin1Char('a'))));
}

void BootCoreTest::firmwareAddArgumentsRejectsUnsafePaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString esp = directory.path() + QStringLiteral("/esp");
    QVERIFY(QDir().mkpath(esp + QStringLiteral("/EFI/fedora")));
    const QString loader = esp + QStringLiteral("/EFI/fedora/shimx64.efi");
    QVERIFY(writeAll(loader, QByteArrayLiteral("not-a-real-binary")));

    QStringList arguments;
    QString error;

    // A valid-looking loader outside any mounted ESP must be rejected.
    QVERIFY(!BootProbe::firmwareAddArguments(loader, QStringLiteral("Fedora"), &arguments, &error));
    QVERIFY(!error.isEmpty());

    // Relative paths, non-.efi files and empty labels are rejected before any probing.
    QVERIFY(!BootProbe::firmwareAddArguments(QStringLiteral("EFI/fedora/shimx64.efi"),
                                             QStringLiteral("Fedora"), &arguments, &error));
    QVERIFY(!BootProbe::firmwareAddArguments(loader, QString(), &arguments, &error));
    QVERIFY(!BootProbe::firmwareAddArguments(loader, QStringLiteral("Bad\nLabel"), &arguments, &error));

    const QString text = esp + QStringLiteral("/EFI/fedora/readme.txt");
    QVERIFY(writeAll(text, QByteArrayLiteral("x")));
    QVERIFY(!BootProbe::firmwareAddArguments(text, QStringLiteral("Fedora"), &arguments, &error));

    QList<EspMount> mounts;
    EspMount mount;
    mount.path = esp;
    mount.source = QStringLiteral("/dev/miryu-nonexistent1");
    mounts.append(mount);
    QVERIFY(!BootProbe::firmwareAddArguments(loader, QStringLiteral("Fedora"), mounts,
                                             &arguments, &error));
}

void BootCoreTest::deviceFingerprintChanges()
{
    BootDevice device;
    device.path = QStringLiteral("/dev/sda1");
    device.type = QStringLiteral("part");
    device.filesystem = QStringLiteral("vfat");
    device.size = QStringLiteral("512M");
    device.partuuid = QStringLiteral("aaaa");
    device.parttype = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");

    const QString before = BootProbe::deviceFingerprint({device});
    QCOMPARE(before.size(), 64);
    QCOMPARE(BootProbe::deviceFingerprint({device}), before);
    QVERIFY(BootProbe::deviceFingerprint({}).size() == 64);

    BootDevice changed = device;
    changed.partuuid = QStringLiteral("bbbb");
    QVERIFY(BootProbe::deviceFingerprint({changed}) != before);

    BootDevice other = device;
    other.path = QStringLiteral("/dev/sda2");
    // The fingerprint is order-independent but must change with the device set.
    QCOMPARE(BootProbe::deviceFingerprint({device, other}),
             BootProbe::deviceFingerprint({other, device}));
    QVERIFY(BootProbe::deviceFingerprint({other}) != before);
}

void BootCoreTest::grubArchitecture()
{
    QCOMPARE(BootProbe::grubTarget(QStringLiteral("x64")), QStringLiteral("x86_64-efi"));
    QCOMPARE(BootProbe::grubTarget(QStringLiteral("aa64")), QStringLiteral("arm64-efi"));
    QCOMPARE(BootProbe::grubTarget(QStringLiteral("ia32")), QStringLiteral("i386-efi"));
    // An empty value falls back to x64; the caller still rejects a missing architecture.
    QCOMPARE(BootProbe::grubTarget(QString()), QStringLiteral("x86_64-efi"));
    QVERIFY(BootProbe::grubTarget(QStringLiteral("mips")).isEmpty());
    QCOMPARE(BootProbe::grubBiosTarget(QStringLiteral("x64")), QStringLiteral("i386-pc"));
    QCOMPARE(BootProbe::grubBiosTarget(QStringLiteral("ia32")), QStringLiteral("i386-pc"));
    QVERIFY(BootProbe::grubBiosTarget(QStringLiteral("aa64")).isEmpty());
    QVERIFY(BootProbe::grubBiosTarget(QString()).isEmpty());
}

void BootCoreTest::backupCreateAndRestore()
{
    Fixture fixture;
    QVERIFY(fixture.root.isValid());
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    snapshot.architecture = QStringLiteral("x64");
    snapshot.bootCurrent = QStringLiteral("0001");
    snapshot.bootOrder = {QStringLiteral("0001")};

    BootBackup backup;
    QString error;
    QVERIFY2(backup.create(snapshot, target, target.root,
                           BootBackup::configurationPaths(target.root),
                           fixture.backups, &error), qPrintable(error));
    QVERIFY(!backup.id().isEmpty());
    QVERIFY(!backup.directory().isEmpty());
    QVERIFY(QFileInfo::exists(backup.directory() + QStringLiteral("/manifest.json")));

    QVERIFY2(backup.record(QStringLiteral("write-grub-config"), true, &error), qPrintable(error));
    QVERIFY2(backup.finish(true, &error), qPrintable(error));

    const QList<BackupInfo> infos = BootBackup::infos(fixture.backups);
    QCOMPARE(infos.size(), 1);
    QCOMPARE(infos.at(0).id, backup.id());
    QCOMPARE(infos.at(0).phase, QStringLiteral("completed"));
    QCOMPARE(infos.at(0).rootDevice, target.rootDevice);
    QVERIFY(infos.at(0).digest == backup.digest());

    const QStringList paths = BootBackup::restorePaths(fixture.backups, backup.id(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(paths.contains(QStringLiteral("/etc/default/grub")));
    QVERIFY(paths.contains(QStringLiteral("/boot/loader/entries/fedora.conf")));

    QVERIFY(writeAll(fixture.grubPath, QByteArrayLiteral("GRUB_TIMEOUT=99\n")));
    QVERIFY2(BootBackup::validateRestore(fixture.backups, backup.id(), backup.digest(), &error),
             qPrintable(error));
    QVERIFY2(BootBackup::restore(fixture.backups, backup.id(), target, target.root, &error),
             qPrintable(error));
    QCOMPARE(readAll(fixture.grubPath), QByteArrayLiteral("GRUB_TIMEOUT=5\n"));
}

void BootCoreTest::backupRejectsTamperedManifest()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(backup.finish(true, &error));
    const QString digest = backup.digest();

    const QString manifestPath = backup.directory() + QStringLiteral("/manifest.json");
    QByteArray manifest = readAll(manifestPath);
    manifest.replace(QByteArrayLiteral("\"version\": 1"), QByteArrayLiteral("\"version\": 1 "));
    QVERIFY(manifest != readAll(manifestPath) || manifest.size() > 0);
    QVERIFY(writeAll(manifestPath, manifest));

    QVERIFY(!BootBackup::validateRestore(fixture.backups, backup.id(), digest, &error));
    QVERIFY(error.contains(QStringLiteral("changed")));
}

void BootCoreTest::backupRejectsTamperedOriginal()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(backup.finish(true, &error));

    const QString original = backup.directory() + QStringLiteral("/files/0.original");
    QVERIFY(QFileInfo::exists(original));
    QVERIFY(writeAll(original, QByteArrayLiteral("tampered\n")));

    QVERIFY(!BootBackup::validateRestore(fixture.backups, backup.id(), backup.digest(), &error));
    QVERIFY(error.contains(QStringLiteral("damaged")));
}

void BootCoreTest::backupRejectsOtherTarget()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(backup.finish(true, &error));

    RepairTarget other = target;
    other.rootDevice = QStringLiteral("/dev/miryu-test1");
    QVERIFY(!BootBackup::validateRestore(fixture.backups, backup.id(), backup.digest(),
                                         other, target.root, &error));
    QVERIFY(!error.isEmpty());
}

void BootCoreTest::backupRejectsDisallowedPath()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(backup.finish(true, &error));

    const QString manifestPath = backup.directory() + QStringLiteral("/manifest.json");
    QJsonObject manifest = QJsonDocument::fromJson(readAll(manifestPath)).object();
    QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    QVERIFY(!files.isEmpty());
    QJsonObject first = files.at(0).toObject();
    first.insert(QStringLiteral("path"), QStringLiteral("/etc/passwd"));
    files.replace(0, first);
    manifest.insert(QStringLiteral("files"), files);
    const QByteArray data = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    QVERIFY(writeAll(manifestPath, data));

    QVERIFY(!BootBackup::validateRestore(fixture.backups, backup.id(), sha256Hex(data), &error));
    QVERIFY(error.contains(QStringLiteral("disallowed")));
}

void BootCoreTest::restoreRefusesModifiedCreatedFile()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    QVERIFY(!QFileInfo::exists(fixture.dropinPath));
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(writeAll(fixture.dropinPath, QByteArrayLiteral("MIRYU=1\n")));
    QVERIFY(backup.record(QStringLiteral("write-grub-config"), true, &error));
    QVERIFY(backup.finish(true, &error));

    // A later manual edit must never be deleted by a restore.
    QVERIFY(writeAll(fixture.dropinPath, QByteArrayLiteral("MIRYU=2\n")));
    QVERIFY(!BootBackup::restore(fixture.backups, backup.id(), target, target.root, &error));
    QVERIFY(error.contains(QStringLiteral("changed")));
    QCOMPARE(readAll(fixture.dropinPath), QByteArrayLiteral("MIRYU=2\n"));
}

void BootCoreTest::restoreRemovesUnmodifiedCreatedFile()
{
    Fixture fixture;
    const RepairTarget target = fixture.target();
    BootSnapshot snapshot;
    BootBackup backup;
    QString error;
    QVERIFY(backup.create(snapshot, target, target.root,
                          BootBackup::configurationPaths(target.root),
                          fixture.backups, &error));
    QVERIFY(writeAll(fixture.dropinPath, QByteArrayLiteral("MIRYU=1\n")));
    QVERIFY(backup.record(QStringLiteral("write-grub-config"), true, &error));
    QVERIFY(backup.finish(true, &error));
    QVERIFY(QFileInfo::exists(fixture.dropinPath));

    QVERIFY2(BootBackup::restore(fixture.backups, backup.id(), target, target.root, &error),
             qPrintable(error));
    QVERIFY(!QFileInfo::exists(fixture.dropinPath));
    QCOMPARE(readAll(fixture.grubPath), QByteArrayLiteral("GRUB_TIMEOUT=5\n"));
}

void BootCoreTest::efiBootVariable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(
        QStringLiteral("Boot0004-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    const QByteArray devicePath = QByteArrayLiteral("\x04\x01\x0c\x00");

    QByteArray header(10, '\0');
    qToLittleEndian<quint32>(0x7u, header.data());
    qToLittleEndian<quint32>(0x1u, header.data() + 4);
    qToLittleEndian<quint16>(static_cast<quint16>(devicePath.size()), header.data() + 8);
    QByteArray description;
    for (const QChar character : QStringLiteral("Fedora")) {
        description.append(static_cast<char>(character.unicode() & 0xff));
        description.append(static_cast<char>(character.unicode() >> 8));
    }
    description.append('\0');
    description.append('\0');
    QVERIFY(writeAll(path, header + description + devicePath));

    QCOMPARE(BootProbe::bootVariablePath(QStringLiteral("0004")),
             QStringLiteral("/sys/firmware/efi/efivars/Boot0004-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    QVERIFY(BootProbe::bootVariablePath(QStringLiteral("4")).isEmpty());
    QVERIFY(BootProbe::bootVariablePath(QStringLiteral("000G")).isEmpty());

    BootProbe::BootVariable variable;
    QString error;
    QVERIFY2(BootProbe::readBootVariable(path, &variable, &error), qPrintable(error));
    QCOMPARE(variable.description, QStringLiteral("Fedora"));
    QVERIFY(variable.active());

    QVERIFY2(BootProbe::rewriteBootVariableDescription(path,
                 QStringLiteral("Evernight Vista"), &error), qPrintable(error));
    QVERIFY2(BootProbe::readBootVariable(path, &variable, &error), qPrintable(error));
    QCOMPARE(variable.description, QStringLiteral("Evernight Vista"));
    QVERIFY(variable.active());
    // The EFI device path and optional data must survive the label rewrite.
    QCOMPARE(variable.raw.mid(variable.devicePathOffset), devicePath);

    QVERIFY2(BootProbe::setBootVariableActive(path, false, &error), qPrintable(error));
    QVERIFY2(BootProbe::readBootVariable(path, &variable, &error), qPrintable(error));
    QVERIFY(!variable.active());
    QCOMPARE(variable.description, QStringLiteral("Evernight Vista"));

    QVERIFY(!BootProbe::rewriteBootVariableDescription(path, QString(), &error));
}

void BootCoreTest::firmwareSecurityVariables()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(QDir(root).mkpath(QStringLiteral("sys/firmware/efi/efivars")));

    const QString efivars = QDir(root).filePath(QStringLiteral("sys/firmware/efi/efivars"));
    const QString secureBoot = QDir(efivars).filePath(
        QStringLiteral("SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    const QString setupMode = QDir(efivars).filePath(
        QStringLiteral("SetupMode-8be4df61-93ca-11d2-aa0d-00e098032b8c"));

    QByteArray enabled(5, '\0');
    enabled[4] = 1;
    QByteArray disabled(5, '\0');
    QVERIFY(writeAll(secureBoot, enabled));
    QVERIFY(writeAll(setupMode, disabled));

    // The trailing slash in the efivars directory must not merge with the name.
    QCOMPARE(BootProbe::firmwareVariable(root, QStringLiteral("SecureBoot")),
             BootSecurityState::Enabled);
    QCOMPARE(BootProbe::firmwareVariable(root, QStringLiteral("SetupMode")),
             BootSecurityState::Disabled);
    QCOMPARE(BootProbe::firmwareVariable(root, QStringLiteral("AuditMode")),
             BootSecurityState::Unknown);

    QByteArray malformed(3, '\0');
    QVERIFY(writeAll(secureBoot, malformed));
    QCOMPARE(BootProbe::firmwareVariable(root, QStringLiteral("SecureBoot")),
             BootSecurityState::Unknown);
}

QTEST_MAIN(BootCoreTest)
#include "testbootcore.moc"
