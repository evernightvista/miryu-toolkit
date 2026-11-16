// White-box tests for the privileged helper. The helper is included so that the
// request validation is exercised without installing or elevating the binary.
#include "boothelper.cpp"

#include <QTemporaryDir>
#include <QtTest>

namespace {

FirmwareEntry entry(const QString &number, const QString &label, bool enabled, bool current = false)
{
    FirmwareEntry value;
    value.number = number;
    value.label = label;
    value.path = QStringLiteral("\\EFI\\test\\grubx64.efi");
    value.partitionUuid = QStringLiteral("3f1a2b4c-5d6e-7f80-9a1b-2c3d4e5f6071");
    value.enabled = enabled;
    value.current = current;
    return value;
}

BootSnapshot snapshotWithOrder()
{
    BootSnapshot snapshot;
    snapshot.bootCurrent = QStringLiteral("0001");
    snapshot.bootOrder = {QStringLiteral("0001"), QStringLiteral("0000")};
    snapshot.firmware = {entry(QStringLiteral("0001"), QStringLiteral("Fedora"), true, true),
                         entry(QStringLiteral("0000"), QStringLiteral("Windows"), true)};
    return snapshot;
}

} // namespace

class BootHelperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sanitizeRedactsSecret();
    void normalizedNumbers();
    void expectedIdentityMustMatch();
    void orderMustBeCompleteAndKnown();
    void orderRejectsInjectedEntries();
    void orderAcceptsPurePermutation();
    void requestTargetRoundTrip();
    void mountPointRejectsEscapes();
    void biosDiskValidation();
    void cleanupWithoutMountsSucceeds();
    void dispatchRejectsInvalidRequests();
};

void BootHelperTest::sanitizeRedactsSecret()
{
    const QString password = QStringLiteral("correct horse battery");
    const QString text = QStringLiteral("mokutil said: ") + password + QStringLiteral(" (again ")
        + password + QLatin1Char(')');
    const QString sanitized = sanitize(text, password);
    QVERIFY(!sanitized.contains(password));
    QCOMPARE(sanitized.count(QStringLiteral("[redacted]")), 2);
    QCOMPARE(sanitize(text, QString()), text);
}

void BootHelperTest::normalizedNumbers()
{
    QCOMPARE(normalizedNumber(QStringLiteral("000a")), QStringLiteral("000A"));
    QCOMPARE(normalizedNumber(QStringLiteral("0001")), QStringLiteral("0001"));
    QVERIFY(normalizedNumber(QStringLiteral("01")).isEmpty());
    QVERIFY(normalizedNumber(QStringLiteral("00001")).isEmpty());
    QVERIFY(normalizedNumber(QStringLiteral("00g1")).isEmpty());
    QVERIFY(normalizedNumber(QStringLiteral("0001\n")).isEmpty());
}

void BootHelperTest::expectedIdentityMustMatch()
{
    const FirmwareEntry *fedora = nullptr;
    BootSnapshot snapshot = snapshotWithOrder();
    fedora = snapshot.entry(QStringLiteral("0001"));
    QVERIFY(fedora);

    QJsonObject request{{QStringLiteral("expectedIdentity"), fedora->identity()}};
    QVERIFY(expectedIdentity(request, fedora));

    const QString staleIdentity = fedora->identity() + QStringLiteral("x");
    request.insert(QStringLiteral("expectedIdentity"), staleIdentity);
    QVERIFY(!expectedIdentity(request, fedora));

    // A stale entry that changed between review and apply must not be accepted.
    FirmwareEntry changed = *fedora;
    changed.label = QStringLiteral("Fedora (renamed)");
    request.insert(QStringLiteral("expectedIdentity"), fedora->identity());
    QVERIFY(!expectedIdentity(request, &changed));

    QVERIFY(!expectedIdentity(QJsonObject{}, fedora));
    QVERIFY(!expectedIdentity(request, nullptr));
}

void BootHelperTest::orderMustBeCompleteAndKnown()
{
    const BootSnapshot snapshot = snapshotWithOrder();
    QStringList arguments;
    QString error;

    QJsonObject request{{QStringLiteral("expectedOrder"),
                         QJsonArray{QStringLiteral("0001"), QStringLiteral("0000")}},
                        {QStringLiteral("order"),
                         QJsonArray{QStringLiteral("0001")}}};
    // A partial order would silently drop firmware entries from BootOrder.
    QVERIFY(!efiOrderArguments(request, snapshot, &arguments, &error));
    QVERIFY(!error.isEmpty());

    request.insert(QStringLiteral("order"),
                   QJsonArray{QStringLiteral("0001"), QStringLiteral("0000"),
                              QStringLiteral("0002")});
    QVERIFY(!efiOrderArguments(request, snapshot, &arguments, &error));

    request.insert(QStringLiteral("order"),
                   QJsonArray{QStringLiteral("0001"), QStringLiteral("0001")});
    QVERIFY(!efiOrderArguments(request, snapshot, &arguments, &error));

    request.insert(QStringLiteral("order"), QJsonArray{QStringLiteral("0001")});
    request.insert(QStringLiteral("expectedOrder"), QJsonArray{QStringLiteral("0000")});
    QVERIFY(!efiOrderArguments(request, snapshot, &arguments, &error));
}

void BootHelperTest::orderRejectsInjectedEntries()
{
    BootSnapshot snapshot = snapshotWithOrder();
    // The firmware already reports an unsorted-but-valid entry.
    snapshot.firmware.append(entry(QStringLiteral("0002"), QStringLiteral("Unused"), true));

    QStringList arguments;
    QString error;
    QJsonObject request{{QStringLiteral("expectedOrder"),
                         QJsonArray{QStringLiteral("0001"), QStringLiteral("0000")}},
                        {QStringLiteral("order"),
                         QJsonArray{QStringLiteral("0001"), QStringLiteral("0000"),
                                    QStringLiteral("0002")}}};
    QVERIFY(!efiOrderArguments(request, snapshot, &arguments, &error));
    QVERIFY(error.contains(QStringLiteral("exactly")));
}

void BootHelperTest::orderAcceptsPurePermutation()
{
    const BootSnapshot snapshot = snapshotWithOrder();
    QStringList arguments;
    QString error;
    QJsonObject request{{QStringLiteral("expectedOrder"),
                         QJsonArray{QStringLiteral("0001"), QStringLiteral("0000")}},
                        {QStringLiteral("order"),
                         QJsonArray{QStringLiteral("0000"), QStringLiteral("0001")}}};
    QVERIFY2(efiOrderArguments(request, snapshot, &arguments, &error), qPrintable(error));
    QCOMPARE(arguments, (QStringList{QStringLiteral("-o"), QStringLiteral("0000,0001")}));
}

void BootHelperTest::requestTargetRoundTrip()
{
    const QJsonObject request{{QStringLiteral("target"),
        QJsonObject{{QStringLiteral("root"), QStringLiteral("/mnt/linux")},
                    {QStringLiteral("rootDevice"), QStringLiteral("/dev/sda3")},
                    {QStringLiteral("automatic"), false},
                    {QStringLiteral("subvolume"), QStringLiteral("@root")},
                    {QStringLiteral("label"), QStringLiteral("Fedora")}}}};
    const RepairTarget target = requestTarget(request);
    QCOMPARE(target.root, QStringLiteral("/mnt/linux"));
    QCOMPARE(target.rootDevice, QStringLiteral("/dev/sda3"));
    QVERIFY(!target.automatic);
    QCOMPARE(target.subvolume, QStringLiteral("@root"));
    QCOMPARE(target.label, QStringLiteral("Fedora"));

    const RepairTarget empty = requestTarget(QJsonObject{});
    QVERIFY(empty.root.isEmpty());
    QVERIFY(empty.rootDevice.isEmpty());
}

void BootHelperTest::mountPointRejectsEscapes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QFileInfo(directory.path()).canonicalFilePath();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/boot")));

    QString error;
    QVERIFY(mountPoint(root, QStringLiteral("/boot"), false, &error));
    QVERIFY(!mountPoint(root, QStringLiteral("/../etc"), false, &error));
    QVERIFY(!mountPoint(root, QStringLiteral("boot"), false, &error));

    QVERIFY(QFile::link(QStringLiteral("/etc"), root + QStringLiteral("/escape")));
    QVERIFY(!mountPoint(root, QStringLiteral("/escape"), false, &error));
    QVERIFY(error.contains(QStringLiteral("symbolic link")));

    QVERIFY(!mountPoint(root, QStringLiteral("/missing"), false, &error));
}

void BootHelperTest::cleanupWithoutMountsSucceeds()
{
    RepairMounts mounts;
    QString error;
    // Nothing was mounted, so cleanup must be a no-op rather than deleting paths.
    QVERIFY(mounts.cleanup(&error));
    QVERIFY(error.isEmpty());
    QVERIFY(mounts.root().isEmpty());
    QVERIFY(mounts.esp().isEmpty());
}

void BootHelperTest::biosDiskValidation()
{
    BootSnapshot snapshot;
    BootDevice disk;
    disk.path = QStringLiteral("/dev/sda");
    disk.type = QStringLiteral("disk");
    disk.partitionTable = QStringLiteral("gpt");
    BootDevice partition;
    partition.path = QStringLiteral("/dev/sda1");
    partition.type = QStringLiteral("part");
    partition.filesystem = QStringLiteral("ext4");
    partition.parent = QStringLiteral("/dev/sda");
    snapshot.devices = {disk, partition};

    RepairTarget target;
    target.root = QStringLiteral("/");
    target.rootDevice = QStringLiteral("/dev/sda1");

    QString error;
    QVERIFY2(validateRepairDisk(QStringLiteral("/dev/sda"), target, snapshot, &error),
             qPrintable(error));
    // A partition is not a whole disk.
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/sda1"), target, snapshot, &error));
    // An unrelated disk must not receive the boot image.
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/sdb"), target, snapshot, &error));
    // Relative and non-canonical paths are refused.
    QVERIFY(!validateRepairDisk(QStringLiteral("sda"), target, snapshot, &error));
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/../dev/sda"), target, snapshot, &error));
    // A root partition on another disk does not authorize this disk.
    target.rootDevice = QStringLiteral("/dev/sdb1");
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/sda"), target, snapshot, &error));

    // Virtual block devices never receive a boot image.
    BootDevice zram;
    zram.path = QStringLiteral("/dev/zram0");
    zram.type = QStringLiteral("disk");
    BootDevice loop;
    loop.path = QStringLiteral("/dev/loop0");
    loop.type = QStringLiteral("disk");
    snapshot.devices.append(zram);
    snapshot.devices.append(loop);
    target.rootDevice = QStringLiteral("/dev/zram0");
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/zram0"), target, snapshot, &error));
    target.rootDevice = QStringLiteral("/dev/loop0");
    QVERIFY(!validateRepairDisk(QStringLiteral("/dev/loop0"), target, snapshot, &error));
}

void BootHelperTest::dispatchRejectsInvalidRequests()
{
    QString error;
    QVERIFY(!applyEfi(QStringLiteral("efi-bogus"), QJsonObject{}, &error));
    QVERIFY(!error.isEmpty());

    // A missing target identity must be rejected before any mount is attempted.
    QVERIFY(!applyRestore(QJsonObject{}, &error));
    QVERIFY(!error.isEmpty());

    // Repair requests are validated against a freshly inspected snapshot.
    QVERIFY(!applyRepair(QJsonObject{}, &error));
    QVERIFY(!error.isEmpty());

    // MOK management refuses to run without UEFI firmware and mokutil.
    error.clear();
    QVERIFY(!applyMok(QStringLiteral("mok-import"), QJsonObject{}, &error));
    QVERIFY(!error.isEmpty());

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QStringLiteral("/request.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("{}") == 2);
    file.close();
    QByteArray payload;
    QVERIFY(readRequest(path, false, &payload, &error));
    QCOMPARE(payload, QByteArrayLiteral("{}"));

    QByteArray stdinPayload;
    QVERIFY(!readRequest(directory.path() + QStringLiteral("/missing.json"), false,
                         &stdinPayload, &error));
}

QTEST_MAIN(BootHelperTest)
#include "testboothelper.moc"
