#include "bootrepairpage.h"

#include "bootbackup.h"
#include "bootdata.h"
#include "bootservice.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int RootRole = Qt::UserRole;
constexpr int RootDeviceRole = Qt::UserRole + 1;
constexpr int AutomaticRole = Qt::UserRole + 2;
constexpr int TargetSubvolumeRole = Qt::UserRole + 3;
constexpr int TargetLabelRole = Qt::UserRole + 4;
constexpr int EspDeviceRole = Qt::UserRole;
constexpr int EspMountRole = Qt::UserRole + 1;
constexpr int EspRelativeRole = Qt::UserRole + 2;
constexpr int BackupIdRole = Qt::UserRole;

const QString espPartitionType = QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b");

QString targetKey(const RepairTarget &target)
{
    return (target.automatic ? QStringLiteral("auto|") : QStringLiteral("mounted|"))
        + target.root + QLatin1Char('|') + target.rootDevice;
}

QString targetDisplay(const RepairTarget &target)
{
    if (target.automatic) {
        return target.label.isEmpty()
            ? i18n("Offline device: %1", target.rootDevice)
            : i18n("Offline device: %1 · %2", target.rootDevice, target.label);
    }
    if (target.root == QLatin1String("/"))
        return i18n("Current system: %1 (%2)", target.root, target.rootDevice);
    return i18n("Mounted system: %1 (%2)", target.root, target.rootDevice);
}

QString espKey(const QComboBox *combo)
{
    return combo->currentData(EspDeviceRole).toString() + QLatin1Char('\n')
        + combo->currentData(EspMountRole).toString();
}

bool efiToolAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("efibootmgr"),
        {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"),
         QStringLiteral("/sbin"), QStringLiteral("/bin")}).isEmpty();
}

QString backupPhase(const QString &phase)
{
    if (phase == QLatin1String("completed"))
        return i18n("Completed");
    if (phase == QLatin1String("incomplete"))
        return i18n("Incomplete");
    if (phase == QLatin1String("prepared"))
        return i18n("Prepared");
    if (phase == QLatin1String("applying"))
        return i18n("Applying");
    return phase;
}

QString operationName(QString action)
{
    if (action.startsWith(QLatin1String("restore:")))
        action = QStringLiteral("restore");
    if (action == QLatin1String("efi-order"))
        return i18n("Change EFI boot order");
    if (action == QLatin1String("efi-next"))
        return i18n("Set or clear the next EFI boot entry");
    if (action == QLatin1String("efi-toggle"))
        return i18n("Enable or disable an EFI boot entry");
    if (action == QLatin1String("efi-rename"))
        return i18n("Rename an EFI boot entry");
    if (action == QLatin1String("efi-remove"))
        return i18n("Remove an EFI boot entry");
    if (action == QLatin1String("efi-add"))
        return i18n("Add an EFI boot entry");
    if (action == QLatin1String("efi-batch"))
        return i18n("Apply EFI boot entry changes");
    if (action == QLatin1String("mok-import"))
        return i18n("Request Machine Owner Key (MOK) enrollment");
    if (action == QLatin1String("mok-revoke"))
        return i18n("Cancel pending Machine Owner Key (MOK) enrollment");
    if (action == QLatin1String("repair"))
        return i18n("Repair the bootloader");
    if (action == QLatin1String("restore"))
        return i18n("Restore boot configuration");
    if (action == QLatin1String("write-grub-config"))
        return i18n("Write GRUB configuration");
    if (action == QLatin1String("grub2-install"))
        return i18n("Install GRUB");
    if (action == QLatin1String("grub2-mkconfig"))
        return i18n("Regenerate GRUB configuration");
    if (action == QLatin1String("firmware-register"))
        return i18n("Register the bootloader in UEFI firmware");
    return i18n("Other boot operation");
}

QString operationDescription(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    const QString action = value.isString() ? value.toString()
        : object.value(QStringLiteral("action")).toString();
    QString result = operationName(action);
    if (object.contains(QStringLiteral("success"))) {
        result += object.value(QStringLiteral("success")).toBool()
            ? i18n(" (succeeded)") : i18n(" (failed)");
    }
    return result;
}

} // namespace

BootRepairPage::BootRepairPage(BootService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *statusLayout = new QHBoxLayout;
    m_statusLabel = new QLabel(i18n("Waiting for the repair target scan…"), this);
    m_statusLabel->setObjectName(QStringLiteral("bootRepairStatus"));
    m_statusLabel->setWordWrap(true);
    m_refreshButton = new QPushButton(i18n("Refresh"), this);
    m_refreshButton->setObjectName(QStringLiteral("refreshBootRepair"));
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_refreshButton);
    root->addLayout(statusLayout);

    auto *repairGroup = new QGroupBox(i18n("GRUB2 UEFI repair"), this);
    m_repairGroup = repairGroup;
    auto *repairLayout = new QVBoxLayout(repairGroup);
    auto *form = new QFormLayout;
    m_form = form;
    m_mode = new QComboBox(repairGroup);
    m_mode->setObjectName(QStringLiteral("repairFirmwareMode"));
    m_mode->addItem(i18n("UEFI"), QStringLiteral("uefi"));
    m_mode->addItem(i18n("BIOS / Legacy"), QStringLiteral("bios"));
    form->addRow(i18n("Firmware mode:"), m_mode);

    m_target = new QComboBox(repairGroup);
    m_target->setObjectName(QStringLiteral("repairTarget"));
    form->addRow(i18n("Linux target:"), m_target);

    m_subvolumeLabel = new QLabel(i18n("Btrfs subvolume:"), repairGroup);
    m_subvolume = new QLineEdit(repairGroup);
    m_subvolume->setObjectName(QStringLiteral("repairSubvolume"));
    m_subvolume->setMaxLength(200);
    m_subvolume->setPlaceholderText(i18n("root"));
    form->addRow(m_subvolumeLabel, m_subvolume);

    m_disk = new QComboBox(repairGroup);
    m_disk->setObjectName(QStringLiteral("repairDisk"));
    form->addRow(i18n("BIOS installation disk:"), m_disk);

    m_esp = new QComboBox(repairGroup);
    m_esp->setObjectName(QStringLiteral("repairEsp"));
    m_esp->setMinimumContentsLength(28);
    m_esp->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    form->addRow(i18n("EFI system partition:"), m_esp);

    m_espRelative = new QComboBox(repairGroup);
    m_espRelative->setObjectName(QStringLiteral("repairEspRelative"));
    m_espRelative->addItem(i18n("/boot/efi"), QStringLiteral("/boot/efi"));
    m_espRelative->addItem(i18n("/efi"), QStringLiteral("/efi"));
    m_espRelative->addItem(i18n("/boot"), QStringLiteral("/boot"));
    form->addRow(i18n("Mount point in target:"), m_espRelative);

    m_bootloaderId = new QLineEdit(QStringLiteral("fedora"), repairGroup);
    m_bootloaderId->setObjectName(QStringLiteral("repairBootloaderId"));
    m_bootloaderId->setMaxLength(80);
    form->addRow(i18n("Bootloader ID:"), m_bootloaderId);

    m_architecture = new QLabel(i18n("Unknown"), repairGroup);
    m_architecture->setObjectName(QStringLiteral("repairArchitecture"));
    m_architectureLabel = new QLabel(i18n("EFI architecture:"), repairGroup);
    form->addRow(m_architectureLabel, m_architecture);

    m_register = new QCheckBox(i18n("Register the installed loader in UEFI firmware"), repairGroup);
    m_register->setObjectName(QStringLiteral("repairRegisterFirmware"));
    m_register->setChecked(true);
    form->addRow(m_register);
    m_fallback = new QCheckBox(i18n("Also install the removable/fallback EFI loader"), repairGroup);
    m_fallback->setObjectName(QStringLiteral("repairFallbackLoader"));
    m_fallback->setChecked(false);
    form->addRow(m_fallback);
    repairLayout->addLayout(form);

    m_diagnostics = new QPlainTextEdit(repairGroup);
    m_diagnostics->setObjectName(QStringLiteral("repairDiagnostics"));
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_diagnostics->setMaximumBlockCount(300);
    m_diagnostics->setMinimumHeight(105);
    m_diagnostics->setMaximumHeight(170);
    repairLayout->addWidget(m_diagnostics);

    m_repairButton = new QPushButton(i18n("Repair GRUB2 UEFI Boot"), repairGroup);
    m_repairButton->setObjectName(QStringLiteral("repairButton"));
    repairLayout->addWidget(m_repairButton, 0, Qt::AlignLeft);
    root->addWidget(repairGroup);

    auto *backupGroup = new QGroupBox(i18n("Configuration backups"), this);
    auto *backupLayout = new QVBoxLayout(backupGroup);
    m_backupTree = new QTreeWidget(backupGroup);
    m_backupTree->setObjectName(QStringLiteral("configurationBackups"));
    m_backupTree->setColumnCount(4);
    m_backupTree->setHeaderLabels({i18n("Time"), i18n("Result"),
                                   i18n("Target"), i18n("Changes")});
    m_backupTree->setRootIsDecorated(false);
    m_backupTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_backupTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_backupTree->setUniformRowHeights(true);
    m_backupTree->setMinimumHeight(150);
    m_backupTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_backupTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_backupTree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_backupTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    backupLayout->addWidget(m_backupTree);

    m_backupDetails = new QPlainTextEdit(backupGroup);
    m_backupDetails->setObjectName(QStringLiteral("configurationBackupDetails"));
    m_backupDetails->setReadOnly(true);
    m_backupDetails->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_backupDetails->setMaximumBlockCount(500);
    m_backupDetails->setMinimumHeight(90);
    m_backupDetails->setMaximumHeight(150);
    backupLayout->addWidget(m_backupDetails);

    auto *restoreNote = new QLabel(
        i18n("Restore replaces only whitelisted configuration originals. It does not restore NVRAM, generated grub.cfg, or EFI binaries."), backupGroup);
    restoreNote->setWordWrap(true);
    backupLayout->addWidget(restoreNote);
    m_restoreButton = new QPushButton(i18n("Restore Selected Configuration"), backupGroup);
    m_restoreButton->setObjectName(QStringLiteral("restoreBackup"));
    backupLayout->addWidget(m_restoreButton, 0, Qt::AlignLeft);
    root->addWidget(backupGroup);

    auto *outputGroup = new QGroupBox(i18n("Operation log"), this);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    m_output = new QPlainTextEdit(outputGroup);
    m_output->setObjectName(QStringLiteral("bootRepairOutput"));
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setMaximumBlockCount(1000);
    m_output->setMinimumHeight(80);
    m_output->setMaximumHeight(140);
    outputLayout->addWidget(m_output);
    root->addWidget(outputGroup);
    root->addStretch();

    connect(m_refreshButton, &QPushButton::clicked,
            m_service, &BootService::refresh);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (m_updating)
                    return;
                updateFirmwareMode();
                updateDiagnostics();
                updateControls();
                updateStatus();
            });
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!m_updating)
                    targetChanged(false);
            });
    connect(m_esp, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!m_updating)
                    espChanged();
            });
    connect(m_espRelative, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateControls(); });
    connect(m_subvolume, &QLineEdit::textChanged, this, [this](const QString &) {
        if (!m_updating) {
            updateDiagnostics();
            updateControls();
        }
    });
    connect(m_disk, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!m_updating) {
                    updateDiagnostics();
                    updateControls();
                }
            });
    connect(m_repairButton, &QPushButton::clicked, this, &BootRepairPage::repair);
    connect(m_restoreButton, &QPushButton::clicked, this, &BootRepairPage::restore);
    connect(m_backupTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (!m_updating) {
            updateBackupDetails();
            updateControls();
        }
    });

    connect(m_service, &BootService::snapshotReady,
            this, [this](const BootSnapshot &) { updateSnapshot(); });
    connect(m_service, &BootService::backupsReady,
            this, [this](const QList<BackupInfo> &) { updateBackups(); });
    connect(m_service, &BootService::scanProgress,
            m_statusLabel, &QLabel::setText);
    connect(m_service, &BootService::busyChanged, this, [this](bool busy) {
        if (busy)
            m_statusLabel->setText(i18n("Please wait for the current boot task…"));
        else
            updateStatus();
        updateControls();
    });
    connect(m_service, &BootService::output, this, [this](const QString &text) {
        const QString cleaned = text.trimmed();
        if (!cleaned.isEmpty()) {
            m_output->appendPlainText(cleaned);
            m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
        }
    });
    connect(m_service, &BootService::operationFinished, this,
            [this](bool success, const QString &message, const QString &action) {
                if (m_ownedAction.isEmpty() || m_ownedAction != action)
                    return;
                m_ownedAction.clear();
                if (success)
                    KMessageBox::information(this, message, i18n("Boot Repair"));
                else
                    KMessageBox::error(this, message, i18n("Boot Repair Failed"));
            });

    updateFirmwareMode();
    updateSnapshot();
    updateBackups();
}

RepairTarget BootRepairPage::currentTarget() const
{
    RepairTarget target;
    target.root = m_target->currentData(RootRole).toString();
    target.rootDevice = m_target->currentData(RootDeviceRole).toString();
    target.automatic = m_target->currentData(AutomaticRole).toBool();
    target.subvolume = m_target->currentData(TargetSubvolumeRole).toString();
    target.label = m_target->currentData(TargetLabelRole).toString();
    if (target.automatic)
        target.subvolume = m_subvolume->text().trimmed();
    return target;
}

QString BootRepairPage::firmwareMode() const
{
    return m_mode->currentData().toString() == QLatin1String("bios")
        ? QStringLiteral("bios") : QStringLiteral("uefi");
}

void BootRepairPage::updateStatus()
{
    const BootSnapshot &snapshot = m_service->snapshot();
    if (snapshot.repairTargets.isEmpty())
        m_statusLabel->setText(i18n("No supported Linux repair targets were found."));
    else if (firmwareMode() == QLatin1String("bios"))
        m_statusLabel->setText(i18n("Legacy BIOS mode: GRUB is written to the master boot record of the selected disk."));
    else
        m_statusLabel->setText(i18n("Linux targets, EFI partitions and configuration backups loaded."));
}

void BootRepairPage::updateFirmwareMode()
{
    const bool bios = firmwareMode() == QLatin1String("bios");
    m_repairGroup->setTitle(bios ? i18n("GRUB2 legacy BIOS repair") : i18n("GRUB2 UEFI repair"));
    m_repairButton->setText(bios ? i18n("Repair GRUB2 BIOS Boot") : i18n("Repair GRUB2 UEFI Boot"));
    m_architectureLabel->setText(bios ? i18n("Architecture:") : i18n("EFI architecture:"));
    m_form->setRowVisible(m_disk, bios);
    m_form->setRowVisible(m_esp, !bios);
    m_form->setRowVisible(m_espRelative, !bios);
    m_form->setRowVisible(m_bootloaderId, !bios);
    m_form->setRowVisible(m_register, !bios);
    m_form->setRowVisible(m_fallback, !bios);
    if (!m_service->snapshot().uefi) {
        m_register->setChecked(false);
    } else if (!bios) {
        m_register->setChecked(true);
    }
}

void BootRepairPage::updateSnapshot()
{
    const QString previousTarget = targetKey(currentTarget());
    const QString previousSubvolume = m_subvolume->text();
    const QString previousEsp = espKey(m_esp);
    const BootSnapshot &snapshot = m_service->snapshot();

    m_updating = true;
    m_target->clear();
    int selectedIndex = -1;
    for (const auto &target : snapshot.repairTargets) {
        m_target->addItem(targetDisplay(target));
        const int index = m_target->count() - 1;
        m_target->setItemData(index, target.root, RootRole);
        m_target->setItemData(index, target.rootDevice, RootDeviceRole);
        m_target->setItemData(index, target.automatic, AutomaticRole);
        m_target->setItemData(index, target.subvolume, TargetSubvolumeRole);
        m_target->setItemData(index, target.label, TargetLabelRole);
        if (targetKey(target) == previousTarget)
            selectedIndex = index;
    }
    m_target->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    m_architecture->setText(snapshot.architecture.isEmpty()
        ? i18n("Unknown") : snapshot.architecture);
    m_mode->setCurrentIndex(snapshot.uefi ? 0 : 1);
    m_updating = false;

    rebuildDisks();
    updateFirmwareMode();
    targetChanged(!previousEsp.isEmpty());
    if (targetKey(currentTarget()) == previousTarget && currentTarget().automatic) {
        m_updating = true;
        m_subvolume->setText(previousSubvolume);
        m_updating = false;
        updateDiagnostics();
        updateControls();
    }
    updateStatus();
}

void BootRepairPage::rebuildDisks()
{
    const RepairTarget target = currentTarget();
    const BootSnapshot &snapshot = m_service->snapshot();
    const QString previous = m_disk->currentData().toString();
    QString preferred = previous;
    for (const auto &device : snapshot.devices) {
        if (device.path == target.rootDevice && !device.parent.isEmpty()) {
            preferred = device.parent;
            break;
        }
    }

    m_updating = true;
    m_disk->clear();
    for (const auto &device : snapshot.devices) {
        if (device.type != QLatin1String("disk")
            || device.path.startsWith(QLatin1String("/dev/zram"))
            || device.path.startsWith(QLatin1String("/dev/loop")))
            continue;
        const QString label = device.label();
        m_disk->addItem(label.isEmpty() ? device.path
                                        : i18n("%1 (%2)", label, device.path),
                        device.path);
    }
    const int index = m_disk->findData(preferred);
    m_disk->setCurrentIndex(index >= 0 ? index : 0);
    m_updating = false;
}

void BootRepairPage::targetChanged(bool preserveEsp)
{
    const QString preferredEsp = preserveEsp ? espKey(m_esp) : QString();
    const RepairTarget target = currentTarget();
    const BootSnapshot &snapshot = m_service->snapshot();
    bool btrfs = false;
    for (const auto &device : snapshot.devices) {
        if (device.path == target.rootDevice) {
            btrfs = device.filesystem == QLatin1String("btrfs");
            break;
        }
    }
    m_updating = true;
    if (target.automatic && btrfs && target.subvolume.isEmpty())
        m_subvolume->setText(QStringLiteral("root"));
    else
        m_subvolume->setText(target.subvolume);
    m_updating = false;
    rebuildDisks();
    rebuildEsp(preferredEsp);
    updateDiagnostics();
    updateBackupDetails();
    updateControls();
}

void BootRepairPage::rebuildEsp(const QString &preferredKey)
{
    const RepairTarget target = currentTarget();
    const BootSnapshot &snapshot = m_service->snapshot();
    const QStringList relativePaths{QStringLiteral("/boot/efi"),
        QStringLiteral("/efi"), QStringLiteral("/boot")};

    m_updating = true;
    m_esp->clear();
    if (target.automatic) {
        for (const auto &device : snapshot.devices) {
            if (device.type != QLatin1String("part")
                || device.filesystem.compare(QLatin1String("vfat"), Qt::CaseInsensitive) != 0
                || device.parttype.compare(espPartitionType, Qt::CaseInsensitive) != 0) {
                continue;
            }
            QString mountedPath;
            for (const auto &mount : snapshot.espMounts) {
                if (mount.source == device.path) {
                    mountedPath = mount.path;
                    break;
                }
            }
            const QString display = mountedPath.isEmpty()
                ? i18n("%1 · %2 (not mounted)", device.path, device.filesystem)
                : i18n("%1 · %2 (mounted at %3)", device.path, device.filesystem, mountedPath);
            m_esp->addItem(display);
            const int index = m_esp->count() - 1;
            m_esp->setItemData(index, device.path, EspDeviceRole);
            m_esp->setItemData(index, mountedPath, EspMountRole);
        }
    } else if (!target.root.isEmpty()) {
        for (const QString &relative : relativePaths) {
            const QString expected = QDir::cleanPath(target.root == QLatin1String("/")
                ? relative : target.root + relative);
            for (const auto &mount : snapshot.espMounts) {
                if (QDir::cleanPath(mount.path) != expected)
                    continue;
                m_esp->addItem(i18n("%1 · %2", mount.source, mount.path));
                const int index = m_esp->count() - 1;
                m_esp->setItemData(index, mount.source, EspDeviceRole);
                m_esp->setItemData(index, mount.path, EspMountRole);
                m_esp->setItemData(index, relative, EspRelativeRole);
            }
        }
    }

    int selectedIndex = -1;
    if (!preferredKey.isEmpty()) {
        for (int index = 0; index < m_esp->count(); ++index) {
            const QString key = m_esp->itemData(index, EspDeviceRole).toString()
                + QLatin1Char('\n') + m_esp->itemData(index, EspMountRole).toString();
            if (key == preferredKey) {
                selectedIndex = index;
                break;
            }
        }
    }
    m_esp->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    m_updating = false;
    espChanged();
}

void BootRepairPage::espChanged()
{
    const RepairTarget target = currentTarget();
    if (!target.automatic) {
        const QString relative = m_esp->currentData(EspRelativeRole).toString();
        if (!relative.isEmpty()) {
            m_updating = true;
            const int index = m_espRelative->findData(relative);
            m_espRelative->setCurrentIndex(index);
            m_updating = false;
        }
    }
    updateControls();
}

void BootRepairPage::updateDiagnostics()
{
    const RepairTarget target = currentTarget();
    if (target.rootDevice.isEmpty()) {
        m_diagnostics->setPlainText(i18n("Select a Linux target to view repair diagnostics."));
        return;
    }
    m_diagnostics->setPlainText(
        BootProbe::repairDiagnostics(target, m_service->snapshot(),
                                     firmwareMode() == QLatin1String("bios"))
            .join(QLatin1Char('\n')));
}

void BootRepairPage::updateBackups()
{
    const BackupInfo *previous = selectedBackup();
    const QString previousId = previous ? previous->id : QString();
    m_updating = true;
    m_backupTree->clear();
    QTreeWidgetItem *select = nullptr;
    for (const auto &backup : m_service->backups()) {
        QStringList changes;
        for (const auto &operation : backup.operations)
            changes.append(operationDescription(operation));
        const QString target = backup.rootDevice.isEmpty()
            ? backup.targetRoot
            : i18n("%1 (%2)", backup.targetRoot, backup.rootDevice);
        auto *item = new QTreeWidgetItem(m_backupTree,
            {backup.created, backupPhase(backup.phase), target, changes.join(i18n(", "))});
        item->setData(0, BackupIdRole, backup.id);
        if (!previousId.isEmpty() && backup.id == previousId)
            select = item;
    }
    if (select)
        m_backupTree->setCurrentItem(select);
    m_updating = false;
    updateBackupDetails();
    updateControls();
}

const BackupInfo *BootRepairPage::selectedBackup() const
{
    const QTreeWidgetItem *item = m_backupTree->currentItem();
    if (!item)
        return nullptr;
    const QString id = item->data(0, BackupIdRole).toString();
    for (const auto &backup : m_service->backups()) {
        if (backup.id == id)
            return &backup;
    }
    return nullptr;
}

void BootRepairPage::updateBackupDetails()
{
    const BackupInfo *backup = selectedBackup();
    if (!backup) {
        m_backupDetails->setPlainText(i18n("Select a configuration backup to view its files and operations."));
        return;
    }

    QStringList details{i18n("Files:")};
    if (backup->files.isEmpty()) {
        details.append(i18n("  No restorable configuration files."));
    } else {
        for (const auto &value : backup->files) {
            const QJsonObject file = value.toObject();
            details.append(file.value(QStringLiteral("existed")).toBool()
                ? i18n("  %1 (original saved)", file.value(QStringLiteral("path")).toString())
                : i18n("  %1 (created by the operation)", file.value(QStringLiteral("path")).toString()));
        }
    }
    details.append(QString());
    details.append(i18n("Operations:"));
    if (backup->operations.isEmpty()) {
        details.append(i18n("  No operations were recorded."));
    } else {
        for (const auto &operation : backup->operations)
            details.append(QLatin1String("  ") + operationDescription(operation));
    }
    m_backupDetails->setPlainText(details.join(QLatin1Char('\n')));
}

void BootRepairPage::updateControls()
{
    const bool idle = !m_service->isBusy();
    const BootSnapshot &snapshot = m_service->snapshot();
    const bool bios = firmwareMode() == QLatin1String("bios");
    const RepairTarget target = currentTarget();
    const BootDevice *rootDevice = nullptr;
    for (const auto &device : snapshot.devices) {
        if (device.path == target.rootDevice) {
            rootDevice = &device;
            break;
        }
    }
    const bool btrfs = rootDevice && rootDevice->filesystem == QLatin1String("btrfs");

    m_mode->setEnabled(idle);
    m_refreshButton->setEnabled(idle);
    m_target->setEnabled(idle);
    m_disk->setEnabled(idle);
    m_esp->setEnabled(idle);
    m_subvolumeLabel->setVisible(target.automatic);
    m_subvolume->setVisible(target.automatic);
    m_subvolume->setEnabled(idle && target.automatic && btrfs);
    m_espRelative->setEnabled(idle && target.automatic && m_esp->currentIndex() >= 0);
    m_bootloaderId->setEnabled(idle);
    m_register->setEnabled(idle && snapshot.uefi);
    m_fallback->setEnabled(idle && snapshot.uefi);
    const bool architectureReady = bios
        ? !BootProbe::grubBiosTarget(snapshot.architecture).isEmpty()
        : !BootProbe::grubTarget(snapshot.architecture).isEmpty();
    const bool locationReady = bios ? m_disk->currentIndex() >= 0 : m_esp->currentIndex() >= 0;
    m_repairButton->setEnabled(idle && !target.rootDevice.isEmpty()
        && architectureReady && locationReady);

    m_backupTree->setEnabled(idle);
    const BackupInfo *backup = selectedBackup();
    const bool matchingTarget = backup && target.root == backup->targetRoot
        && target.rootDevice == backup->rootDevice;
    m_restoreButton->setEnabled(idle && backup && !backup->files.isEmpty()
        && !backup->digest.isEmpty() && matchingTarget);
}

QJsonObject BootRepairPage::targetRequest(const RepairTarget &target) const
{
    return {{QStringLiteral("root"), target.root},
            {QStringLiteral("rootDevice"), target.rootDevice},
            {QStringLiteral("automatic"), target.automatic},
            {QStringLiteral("subvolume"), target.subvolume},
            {QStringLiteral("label"), target.label}};
}

void BootRepairPage::submit(const QString &action, const QJsonObject &request,
                            const QString &description, const QString &confirmation)
{
    if (KMessageBox::questionTwoActions(this, confirmation, i18n("Confirm Boot Repair"),
                                        KGuiItem(i18n("Yes")), KGuiItem(i18n("No")))
        != KMessageBox::PrimaryAction) {
        return;
    }
    m_ownedAction = action;
    if (!m_service->apply(action, request, description))
        m_ownedAction.clear();
}

void BootRepairPage::repair()
{
    const RepairTarget target = currentTarget();
    const BootSnapshot &snapshot = m_service->snapshot();
    if (target.automatic && !BootProbe::validSubvolume(target.subvolume)) {
        KMessageBox::error(this,
            i18n("Enter a valid relative Btrfs subvolume name, or leave it empty."),
            i18n("Invalid Subvolume"));
        return;
    }
    if (firmwareMode() == QLatin1String("bios")) {
        const QString disk = m_disk->currentData().toString();
        if (BootProbe::grubBiosTarget(snapshot.architecture).isEmpty()) {
            KMessageBox::error(this,
                i18n("Legacy BIOS repair is available on x86 systems only."),
                i18n("Unsupported Architecture"));
            return;
        }
        if (disk.isEmpty()) {
            KMessageBox::error(this,
                i18n("Select the disk that contains the target root partition."),
                i18n("No Installation Disk"));
            return;
        }
        const QJsonObject biosRequest{
            {QStringLiteral("target"), targetRequest(target)},
            {QStringLiteral("disk"), disk},
            {QStringLiteral("architecture"), snapshot.architecture},
            {QStringLiteral("expectedDeviceFingerprint"), BootProbe::deviceFingerprint(snapshot.devices)},
            {QStringLiteral("loader"), QStringLiteral("grub")},
            {QStringLiteral("mode"), QStringLiteral("bios")}
        };
        const QString biosConfirmation = i18n(
            "Repair GRUB2 legacy BIOS boot with these settings?\n\nTarget: %1\nInstallation disk: %2\nArchitecture: %3\n\nThis writes the GRUB2 boot image to the master boot record of the selected disk.",
            target.root, disk, snapshot.architecture);
        submit(QStringLiteral("repair"), biosRequest,
               i18n("Repair the GRUB2 legacy BIOS boot configuration"), biosConfirmation);
        return;
    }

    static const QRegularExpression identifier(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.+-]{0,79}$"));
    if (!identifier.match(m_bootloaderId->text()).hasMatch()) {
        KMessageBox::error(this,
            i18n("The bootloader ID must contain 1 to 80 safe ASCII letters, digits, dots, underscores, plus signs or hyphens."),
            i18n("Invalid Bootloader ID"));
        return;
    }
    if (snapshot.architecture.isEmpty()
        || BootProbe::grubTarget(snapshot.architecture).isEmpty()) {
        KMessageBox::error(this,
            i18n("The host EFI architecture could not be determined for GRUB2 repair."),
            i18n("Unsupported Architecture"));
        return;
    }
    if (m_register->isChecked() && (!snapshot.uefi || !efiToolAvailable())) {
        KMessageBox::error(this,
            i18n("Disable firmware registration, or repair on a UEFI host with efibootmgr installed."),
            i18n("Firmware Registration Unavailable"));
        return;
    }

    const QString espDevice = m_esp->currentData(EspDeviceRole).toString();
    const QString espPath = m_esp->currentData(EspMountRole).toString();
    const QString espRelative = m_espRelative->currentData().toString();
    if (espDevice.isEmpty() || espRelative.isEmpty()) {
        KMessageBox::error(this,
            i18n("Select a usable EFI system partition and target mount point."),
            i18n("No EFI System Partition"));
        return;
    }

    const QJsonObject request{
        {QStringLiteral("target"), targetRequest(target)},
        {QStringLiteral("espPath"), espPath},
        {QStringLiteral("espDevice"), espDevice},
        {QStringLiteral("espRelative"), espRelative},
        {QStringLiteral("bootloaderId"), m_bootloaderId->text()},
        {QStringLiteral("register"), m_register->isChecked()},
        {QStringLiteral("fallback"), m_fallback->isChecked()},
        {QStringLiteral("architecture"), snapshot.architecture},
        {QStringLiteral("expectedDeviceFingerprint"), BootProbe::deviceFingerprint(snapshot.devices)},
        {QStringLiteral("loader"), QStringLiteral("grub")},
        {QStringLiteral("mode"), QStringLiteral("uefi")}
    };
    const QString confirmation = i18n(
        "Repair GRUB2 UEFI boot with these settings?\n\nTarget: %1\nESP device: %2\nExisting ESP mount: %3\nTarget mount point: %4\nBootloader ID: %5\nRegister firmware entry: %6\nFallback loader: %7\nArchitecture: %8",
        target.root, espDevice,
        espPath.isEmpty() ? i18n("None; mount the selected ESP") : espPath,
        espRelative, m_bootloaderId->text(),
        m_register->isChecked() ? i18n("Yes") : i18n("No"),
        m_fallback->isChecked() ? i18n("Yes") : i18n("No"),
        snapshot.architecture);
    submit(QStringLiteral("repair"), request,
           i18n("Repair the GRUB2 UEFI boot configuration"), confirmation);
}

void BootRepairPage::restore()
{
    const BackupInfo *backup = selectedBackup();
    const RepairTarget target = currentTarget();
    if (!backup || backup->files.isEmpty() || target.root != backup->targetRoot
        || target.rootDevice != backup->rootDevice) {
        return;
    }

    const QJsonObject request{
        {QStringLiteral("backupId"), backup->id},
        {QStringLiteral("digest"), backup->digest},
        {QStringLiteral("target"), targetRequest(target)}
    };
    const QString confirmation = i18n(
        "Restore configuration originals from backup %1 (%2) to %3?\n\nOnly whitelisted configuration originals are restored. NVRAM, generated grub.cfg and EFI binaries are not restored.",
        backup->id, backup->created, target.root);
    submit(QStringLiteral("restore"), request,
           i18n("Restore configuration originals from a backup"), confirmation);
}
