#include "bootrepairpage.h"
#include "bootservice.h"
#include "mainwindow.h"
#include "uefibootpage.h"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QtTest>

void dragHeaderTo(QHeaderView *header, int sourceLogical, int targetLogical)
{
    auto *viewport = header->viewport();
    const auto sectionCenter = [header](int logical) {
        return QPoint(header->sectionViewportPosition(logical) + header->sectionSize(logical) / 2,
                      header->height() / 2);
    };
    const QPoint source = sectionCenter(sourceLogical);
    const QPoint target = sectionCenter(targetLogical);
    QTest::mouseMove(viewport, source);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, source);
    QTest::mouseMove(viewport, target);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, target);
}

void resizeHeaderSection(QHeaderView *header, int logical, int delta)
{
    auto *viewport = header->viewport();
    const QPoint source(header->sectionViewportPosition(logical) + header->sectionSize(logical),
                        header->height() / 2);
    const QPoint target = source + QPoint(delta, 0);
    QTest::mouseMove(viewport, source);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, source);
    QTest::mouseMove(viewport, target);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, target);
}

class BootPagesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tabOrder();
    void nonUefiState();
    void biosModeState();
    void firmwareModeSwitch();
    void secureBootDisabledMok();
    void mokColumnInteraction();
    void hideUefiTabOnBios();
    void stagedEfiChanges();
};

void BootPagesTest::tabOrder()
{
    MainWindow window;
    auto *tabs = window.findChild<QTabWidget *>();
    QVERIFY(tabs);
    QVERIFY(tabs->count() >= 6);
    QCOMPARE(tabs->tabText(1), QStringLiteral("Boot Menu"));
    QCOMPARE(tabs->tabText(2), QStringLiteral("UEFI Entries && Secure Boot"));
    QCOMPARE(tabs->tabText(3), QStringLiteral("Boot Repair"));
    QVERIFY(window.findChild<UefiBootPage *>());
    QVERIFY(window.findChild<BootRepairPage *>());
    QCOMPARE(window.findChildren<BootService *>().size(), 1);
}

void BootPagesTest::nonUefiState()
{
    BootService service;
    UefiBootPage firmware(&service);
    BootRepairPage repair(&service);

    QVERIFY(!service.snapshot().uefi);
    auto *status = firmware.findChild<QLabel *>(QStringLiteral("uefiBootStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("not booted in UEFI mode")));
    auto *add = firmware.findChild<QPushButton *>(QStringLiteral("addBootEntry"));
    QVERIFY(add);
    QVERIFY(!add->isEnabled());

    auto *targets = repair.findChild<QComboBox *>(QStringLiteral("repairTarget"));
    auto *repairButton = repair.findChild<QPushButton *>(QStringLiteral("repairButton"));
    auto *restoreButton = repair.findChild<QPushButton *>(QStringLiteral("restoreBackup"));
    QVERIFY(targets);
    QVERIFY(repairButton);
    QVERIFY(restoreButton);
    QCOMPARE(targets->count(), 0);
    QVERIFY(!repairButton->isEnabled());
    QVERIFY(!restoreButton->isEnabled());
}

void BootPagesTest::biosModeState()
{
    BootService service;
    BootRepairPage repair(&service);

    auto *disk = repair.findChild<QComboBox *>(QStringLiteral("repairDisk"));
    auto *esp = repair.findChild<QComboBox *>(QStringLiteral("repairEsp"));
    auto *bootloader = repair.findChild<QLineEdit *>(QStringLiteral("repairBootloaderId"));
    auto *button = repair.findChild<QPushButton *>(QStringLiteral("repairButton"));
    QVERIFY(disk);
    QVERIFY(esp);
    QVERIFY(bootloader);
    QVERIFY(button);

    // Legacy BIOS repair installs to a disk, so ESP and bootloader ID are hidden.
    QVERIFY(!disk->isHidden());
    QVERIFY(esp->isHidden());
    QVERIFY(bootloader->isHidden());
    QCOMPARE(button->text(), QStringLiteral("Repair GRUB2 BIOS Boot"));
}

void BootPagesTest::firmwareModeSwitch()
{
    BootService service;
    BootRepairPage repair(&service);

    auto *mode = repair.findChild<QComboBox *>(QStringLiteral("repairFirmwareMode"));
    auto *disk = repair.findChild<QComboBox *>(QStringLiteral("repairDisk"));
    auto *esp = repair.findChild<QComboBox *>(QStringLiteral("repairEsp"));
    auto *bootloader = repair.findChild<QLineEdit *>(QStringLiteral("repairBootloaderId"));
    auto *button = repair.findChild<QPushButton *>(QStringLiteral("repairButton"));
    QVERIFY(mode);
    QVERIFY(disk);
    QVERIFY(esp);
    QVERIFY(bootloader);
    QVERIFY(button);

    QCOMPARE(mode->count(), 2);
    QCOMPARE(mode->itemData(0).toString(), QStringLiteral("uefi"));
    QCOMPARE(mode->itemData(1).toString(), QStringLiteral("bios"));
    // The default follows a host that was not booted in UEFI mode.
    QCOMPARE(mode->currentData().toString(), QStringLiteral("bios"));

    // Switching to UEFI on demand shows the ESP and bootloader fields again.
    mode->setCurrentIndex(mode->findData(QStringLiteral("uefi")));
    QVERIFY(disk->isHidden());
    QVERIFY(!esp->isHidden());
    QVERIFY(!bootloader->isHidden());
    QCOMPARE(button->text(), QStringLiteral("Repair GRUB2 UEFI Boot"));

    mode->setCurrentIndex(mode->findData(QStringLiteral("bios")));
    QVERIFY(!disk->isHidden());
    QVERIFY(esp->isHidden());
    QVERIFY(bootloader->isHidden());
    QCOMPARE(button->text(), QStringLiteral("Repair GRUB2 BIOS Boot"));
}

void BootPagesTest::secureBootDisabledMok()
{
    BootService service;
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.secureBoot = BootSecurityState::Disabled;
    snapshot.setupMode = BootSecurityState::Disabled;
    service.setSnapshotForTest(snapshot);

    UefiBootPage disabled(&service);
    auto *disabledArea = disabled.findChild<QWidget *>(QStringLiteral("mokArea"));
    QVERIFY(disabledArea);
    QVERIFY(!disabledArea->isEnabled());
    auto *request = disabled.findChild<QPushButton *>(QStringLiteral("requestMokEnrollment"));
    QVERIFY(request);
    QVERIFY(!request->isEnabled());

    snapshot.secureBoot = BootSecurityState::Enabled;
    service.setSnapshotForTest(snapshot);
    UefiBootPage enabled(&service);
    auto *enabledArea = enabled.findChild<QWidget *>(QStringLiteral("mokArea"));
    QVERIFY(enabledArea);
    QVERIFY(enabledArea->isEnabled());
}

void BootPagesTest::mokColumnInteraction()
{
    BootService service;
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.secureBoot = BootSecurityState::Enabled;

    MokCertificate certificate;
    certificate.commonName = QStringLiteral("Miryu Test CA");
    certificate.issuer = QStringLiteral("CN=Miryu Test CA, O=Miryu, OU=")
        + QString(600, QLatin1Char('X'));
    certificate.notAfter = QStringLiteral("Nov 15 00:00:00 2030 GMT");
    certificate.fingerprint = QStringLiteral("11:22:33:44:55:66:77:88:99:00");
    snapshot.enrolledCertificates = {certificate};
    snapshot.pendingCertificates = {certificate};
    service.setSnapshotForTest(snapshot);

    UefiBootPage page(&service);
    auto *tree = page.findChild<QTreeWidget *>(QStringLiteral("mokCertificates"));
    QVERIFY(tree);
    QVERIFY(tree->header()->sectionsMovable());
    QVERIFY(tree->header()->stretchLastSection());
    QTest::mouseMove(tree->header()->viewport(),
        QPoint(tree->header()->sectionViewportPosition(1) + tree->header()->sectionSize(1) / 2,
               tree->header()->height() / 2));
    QCOMPARE(tree->header()->cursor().shape(), Qt::ArrowCursor);
    QTest::mouseMove(tree->header()->viewport(),
        QPoint(tree->header()->sectionViewportPosition(1) + tree->header()->sectionSize(1),
               tree->header()->height() / 2));
    QCOMPARE(tree->header()->cursor().shape(), Qt::SplitHCursor);
    QCOMPARE(tree->topLevelItemCount(), 2);

    for (int group = 0; group < tree->topLevelItemCount(); ++group) {
        QCOMPARE(tree->topLevelItem(group)->childCount(), 1);
        const auto *item = tree->topLevelItem(group)->child(0);
        for (int column = 0; column < tree->columnCount(); ++column) {
            QVERIFY(!item->text(column).isEmpty());
            const QString toolTip = item->toolTip(column);
            QCOMPARE(QString(toolTip).remove(QLatin1Char('\n')), item->text(column));
        }
        QVERIFY(item->toolTip(1).contains(QLatin1Char('\n')));
    }

    dragHeaderTo(tree->header(), 1, 0);
    QCOMPARE(tree->header()->visualIndex(1), 0);
    for (int column = 0; column < tree->columnCount(); ++column)
        QCOMPARE(tree->header()->sectionResizeMode(column), QHeaderView::Interactive);
    const int originalWidth = tree->header()->sectionSize(1);
    resizeHeaderSection(tree->header(), 1, 40);
    QVERIFY(tree->header()->sectionSize(1) >= originalWidth + 20);
}

void BootPagesTest::hideUefiTabOnBios()
{
    MainWindow window;
    auto *tabs = window.findChild<QTabWidget *>();
    auto *page = window.findChild<UefiBootPage *>();
    auto *service = window.findChild<BootService *>();
    QVERIFY(tabs);
    QVERIFY(page);
    QVERIFY(service);

    const int index = [tabs]() {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i).startsWith(QStringLiteral("UEFI")))
                return i;
        }
        return -1;
    }();
    QVERIFY(index >= 0);
    // Hidden until a scan confirms UEFI firmware.
    QVERIFY(!tabs->isTabVisible(index));

    BootSnapshot snapshot;
    snapshot.uefi = true;
    service->snapshotReady(snapshot);
    QVERIFY(tabs->isTabVisible(index));

    snapshot.uefi = false;
    service->snapshotReady(snapshot);
    QVERIFY(!tabs->isTabVisible(index));
}

void BootPagesTest::stagedEfiChanges()
{
    BootService service;
    BootSnapshot snapshot;
    snapshot.uefi = true;
    snapshot.bootCurrent = QStringLiteral("0001");
    snapshot.bootOrder = {QStringLiteral("0001"), QStringLiteral("0002")};
    FirmwareEntry first;
    first.number = QStringLiteral("0001");
    first.label = QStringLiteral("Fedora");
    first.enabled = true;
    first.current = true;
    FirmwareEntry second;
    second.number = QStringLiteral("0002");
    second.label = QStringLiteral("Windows Boot Manager");
    second.enabled = true;
    snapshot.firmware = {first, second};
    service.setSnapshotForTest(snapshot);

    UefiBootPage page(&service);
    page.setToolAvailabilityForTest(true, true);

    auto *table = page.findChild<QTableWidget *>(QStringLiteral("efiTable"));
    auto *toggle = page.findChild<QPushButton *>(QStringLiteral("toggleBootEntry"));
    auto *save = page.findChild<QPushButton *>(QStringLiteral("saveEfiChanges"));
    auto *discard = page.findChild<QPushButton *>(QStringLiteral("discardEfiChanges"));
    auto *pending = page.findChild<QLabel *>(QStringLiteral("pendingEfiChanges"));
    QVERIFY(table);
    QVERIFY(table->horizontalHeader()->sectionsMovable());
    QVERIFY(table->horizontalHeader()->stretchLastSection());
    QTest::mouseMove(table->horizontalHeader()->viewport(),
        QPoint(table->horizontalHeader()->sectionViewportPosition(1)
                   + table->horizontalHeader()->sectionSize(1) / 2,
               table->horizontalHeader()->height() / 2));
    QCOMPARE(table->horizontalHeader()->cursor().shape(), Qt::ArrowCursor);
    QTest::mouseMove(table->horizontalHeader()->viewport(),
        QPoint(table->horizontalHeader()->sectionViewportPosition(1)
                   + table->horizontalHeader()->sectionSize(1),
               table->horizontalHeader()->height() / 2));
    QCOMPARE(table->horizontalHeader()->cursor().shape(), Qt::SplitHCursor);
    dragHeaderTo(table->horizontalHeader(), 0, 1);
    QCOMPARE(table->horizontalHeader()->visualIndex(0), 1);
    for (int column = 0; column < table->columnCount(); ++column)
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(column), QHeaderView::Interactive);
    const int originalWidth = table->horizontalHeader()->sectionSize(1);
    resizeHeaderSection(table->horizontalHeader(), 1, 40);
    QVERIFY(table->horizontalHeader()->sectionSize(1) >= originalWidth + 20);
    QVERIFY(toggle);
    QVERIFY(save);
    QVERIFY(discard);
    QVERIFY(pending);

    QVERIFY(!save->isEnabled());
    QVERIFY(pending->isHidden());

    table->setCurrentCell(1, 0);
    QVERIFY(toggle->isEnabled());
    toggle->click();

    // The edit is staged: nothing is sent to the privileged helper yet.
    QVERIFY(!service.isBusy());
    QVERIFY(save->isEnabled());
    QVERIFY(!pending->isHidden());

    discard->click();
    QVERIFY(!save->isEnabled());
    QVERIFY(pending->isHidden());
}

QTEST_MAIN(BootPagesTest)
#include "testbootpages.moc"
