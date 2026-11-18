#include "uefibootpage.h"

#include "bootdata.h"
#include "bootservice.h"

#include <KGuiItem>
#include <KLocalizedString>
#include <KMessageBox>
#include <KStandardGuiItem>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int NumberRole = Qt::UserRole;
constexpr int IdentityRole = Qt::UserRole + 1;
constexpr int InOrderRole = Qt::UserRole + 2;

QString wrappedToolTip(const QString &text, const QFontMetrics &metrics)
{
    constexpr int MaximumLineWidth = 520;
    QString result;
    QString line;
    for (const QChar character : text) {
        if (character == QLatin1Char('\n')) {
            result += line + QLatin1Char('\n');
            line.clear();
            continue;
        }
        if (!line.isEmpty()
            && metrics.horizontalAdvance(line + character) > MaximumLineWidth) {
            result += line + QLatin1Char('\n');
            line.clear();
        }
        line += character;
    }
    result += line;
    return result;
}

bool toolAvailable(const QString &name)
{
    return !QStandardPaths::findExecutable(name,
        {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"),
         QStringLiteral("/sbin"), QStringLiteral("/bin")}).isEmpty();
}

QString securityText(BootSecurityState state)
{
    switch (state) {
    case BootSecurityState::Disabled:
        return i18n("Disabled");
    case BootSecurityState::Enabled:
        return i18n("Enabled");
    case BootSecurityState::Unsupported:
        return i18n("Unsupported");
    case BootSecurityState::Unknown:
        break;
    }
    return i18n("Unknown");
}

bool validLabel(const QString &label)
{
    if (label.isEmpty() || label.size() > 80 || label.trimmed().isEmpty())
        return false;
    for (const QChar character : label) {
        if (!character.isPrint() || character == QLatin1Char('\n')
            || character == QLatin1Char('\r')) {
            return false;
        }
    }
    return true;
}

QString certificateName(const MokCertificate &certificate)
{
    if (!certificate.commonName.isEmpty())
        return certificate.commonName;
    if (!certificate.subject.isEmpty())
        return certificate.subject;
    return i18n("Unnamed certificate");
}

} // namespace

UefiBootPage::UefiBootPage(BootService *service, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *statusLayout = new QHBoxLayout;
    m_statusLabel = new QLabel(i18n("Waiting for the boot configuration scan…"), this);
    m_statusLabel->setObjectName(QStringLiteral("uefiBootStatus"));
    m_statusLabel->setWordWrap(true);
    m_refreshButton = new QPushButton(i18n("Refresh"), this);
    m_refreshButton->setObjectName(QStringLiteral("refreshUefiBoot"));
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_refreshButton);
    root->addLayout(statusLayout);

    m_noticeLabel = new QLabel(this);
    m_noticeLabel->setObjectName(QStringLiteral("uefiBootNotices"));
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->hide();
    root->addWidget(m_noticeLabel);

    auto *firmwareGroup = new QGroupBox(i18n("UEFI firmware boot entries"), this);
    auto *firmwareLayout = new QVBoxLayout(firmwareGroup);
    m_table = new QTableWidget(firmwareGroup);
    m_table->setObjectName(QStringLiteral("efiTable"));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({i18n("Boot ID"), i18n("Name"),
        i18n("PARTUUID"), i18n("State")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->horizontalHeader()->setMinimumSectionSize(70);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 260);
    m_table->setColumnWidth(2, 360);
    m_table->setColumnWidth(3, 110);
    m_table->setMinimumHeight(190);
    firmwareLayout->addWidget(m_table);

    auto *buttonGrid = new QGridLayout;
    m_moveUpButton = new QPushButton(i18n("Move up"), firmwareGroup);
    m_moveUpButton->setObjectName(QStringLiteral("moveBootEntryUp"));
    m_moveDownButton = new QPushButton(i18n("Move down"), firmwareGroup);
    m_moveDownButton->setObjectName(QStringLiteral("moveBootEntryDown"));
    m_onceButton = new QPushButton(i18n("Boot once"), firmwareGroup);
    m_onceButton->setObjectName(QStringLiteral("bootEntryOnce"));
    m_clearNextButton = new QPushButton(i18n("Clear next boot"), firmwareGroup);
    m_clearNextButton->setObjectName(QStringLiteral("clearNextBootEntry"));
    m_toggleButton = new QPushButton(i18n("Enable / Disable"), firmwareGroup);
    m_toggleButton->setObjectName(QStringLiteral("toggleBootEntry"));
    m_renameButton = new QPushButton(i18n("Rename"), firmwareGroup);
    m_renameButton->setObjectName(QStringLiteral("renameBootEntry"));
    m_addButton = new QPushButton(i18n("Add EFI entry"), firmwareGroup);
    m_addButton->setObjectName(QStringLiteral("addBootEntry"));
    m_removeButton = new QPushButton(i18n("Remove"), firmwareGroup);
    m_removeButton->setObjectName(QStringLiteral("removeBootEntry"));
    buttonGrid->addWidget(m_moveUpButton, 0, 0);
    buttonGrid->addWidget(m_moveDownButton, 0, 1);
    buttonGrid->addWidget(m_onceButton, 0, 2);
    buttonGrid->addWidget(m_clearNextButton, 0, 3);
    buttonGrid->addWidget(m_toggleButton, 1, 0);
    buttonGrid->addWidget(m_renameButton, 1, 1);
    buttonGrid->addWidget(m_addButton, 1, 2);
    buttonGrid->addWidget(m_removeButton, 1, 3);
    firmwareLayout->addLayout(buttonGrid);

    m_pendingLabel = new QLabel(firmwareGroup);
    m_pendingLabel->setObjectName(QStringLiteral("pendingEfiChanges"));
    m_pendingLabel->setWordWrap(true);
    m_pendingLabel->hide();
    firmwareLayout->addWidget(m_pendingLabel);

    auto *saveRow = new QHBoxLayout;
    m_saveButton = new QPushButton(i18n("Save changes"), firmwareGroup);
    m_saveButton->setObjectName(QStringLiteral("saveEfiChanges"));
    m_discardButton = new QPushButton(i18n("Discard changes"), firmwareGroup);
    m_discardButton->setObjectName(QStringLiteral("discardEfiChanges"));
    saveRow->addWidget(m_saveButton);
    saveRow->addWidget(m_discardButton);
    saveRow->addStretch();
    firmwareLayout->addLayout(saveRow);
    root->addWidget(firmwareGroup);

    auto *securityGroup = new QGroupBox(i18nd("miryu-toolkit", "Secure Boot and Machine Owner Keys"), this);
    auto *securityLayout = new QVBoxLayout(securityGroup);
    auto *securityForm = new QFormLayout;
    m_secureBootValue = new QLabel(securityText(BootSecurityState::Unknown), securityGroup);
    m_secureBootValue->setObjectName(QStringLiteral("secureBootState"));
    m_setupModeValue = new QLabel(securityText(BootSecurityState::Unknown), securityGroup);
    m_setupModeValue->setObjectName(QStringLiteral("setupModeState"));
    m_moduleSignatureValue = new QLabel(securityText(BootSecurityState::Unknown), securityGroup);
    m_moduleSignatureValue->setObjectName(QStringLiteral("moduleSignatureState"));
    securityForm->addRow(i18nd("miryu-toolkit", "SecureBoot:"), m_secureBootValue);
    securityForm->addRow(i18n("SetupMode:"), m_setupModeValue);
    securityForm->addRow(i18n("moduleSigEnforce:"), m_moduleSignatureValue);
    securityLayout->addLayout(securityForm);

    m_mokArea = new QWidget(securityGroup);
    m_mokArea->setObjectName(QStringLiteral("mokArea"));
    auto *mokLayout = new QVBoxLayout(m_mokArea);
    mokLayout->setContentsMargins(0, 0, 0, 0);
    securityLayout->addWidget(m_mokArea);

    m_mokTree = new QTreeWidget(m_mokArea);
    m_mokTree->setObjectName(QStringLiteral("mokCertificates"));
    m_mokTree->setColumnCount(4);
    m_mokTree->setHeaderLabels({i18n("Name"), i18n("Issuer"),
                                i18n("Expires"), i18n("SHA1")});
    m_mokTree->setRootIsDecorated(true);
    m_mokTree->setUniformRowHeights(true);
    m_mokTree->setMinimumHeight(130);
    m_mokTree->header()->setSectionsMovable(true);
    m_mokTree->header()->setMinimumSectionSize(80);
    m_mokTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_mokTree->header()->setStretchLastSection(true);
    m_mokTree->setColumnWidth(0, 260);
    m_mokTree->setColumnWidth(1, 300);
    m_mokTree->setColumnWidth(2, 190);
    m_mokTree->setColumnWidth(3, 280);
    mokLayout->addWidget(m_mokTree);

    m_rawGroup = new QGroupBox(i18n("Raw MOK output"), securityGroup);
    m_rawGroup->setObjectName(QStringLiteral("rawMokOutputGroup"));
    m_rawGroup->setCheckable(true);
    m_rawGroup->setChecked(false);
    auto *rawLayout = new QVBoxLayout(m_rawGroup);
    m_rawMok = new QPlainTextEdit(m_rawGroup);
    m_rawMok->setObjectName(QStringLiteral("rawMokOutput"));
    m_rawMok->setReadOnly(true);
    m_rawMok->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rawMok->setMinimumHeight(90);
    m_rawMok->setMaximumHeight(160);
    m_rawMok->hide();
    rawLayout->addWidget(m_rawMok);
    mokLayout->addWidget(m_rawGroup);

    auto *mokButtons = new QHBoxLayout;
    m_requestMokButton = new QPushButton(i18n("Request enrollment"), securityGroup);
    m_requestMokButton->setObjectName(QStringLiteral("requestMokEnrollment"));
    m_cancelMokButton = new QPushButton(i18n("Cancel pending enrollment"), securityGroup);
    m_cancelMokButton->setObjectName(QStringLiteral("cancelMokEnrollment"));
    mokButtons->addWidget(m_requestMokButton);
    mokButtons->addWidget(m_cancelMokButton);
    mokButtons->addStretch();
    mokLayout->addLayout(mokButtons);
    root->addWidget(securityGroup);

    auto *outputGroup = new QGroupBox(i18n("Operation log"), this);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    m_output = new QPlainTextEdit(outputGroup);
    m_output->setObjectName(QStringLiteral("uefiBootOutput"));
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
    connect(m_rawGroup, &QGroupBox::toggled,
            m_rawMok, &QWidget::setVisible);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        if (m_updating)
            return;
        updateControls();
    });
    connect(m_moveUpButton, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_saveButton, &QPushButton::clicked, this, &UefiBootPage::saveChanges);
    connect(m_discardButton, &QPushButton::clicked, this, &UefiBootPage::discardChanges);
    connect(m_moveDownButton, &QPushButton::clicked, this, [this]() { moveSelected(1); });
    connect(m_onceButton, &QPushButton::clicked, this, &UefiBootPage::setNextBoot);
    connect(m_clearNextButton, &QPushButton::clicked, this, &UefiBootPage::clearNextBoot);
    connect(m_toggleButton, &QPushButton::clicked, this, &UefiBootPage::toggleSelected);
    connect(m_renameButton, &QPushButton::clicked, this, &UefiBootPage::renameSelected);
    connect(m_addButton, &QPushButton::clicked, this, &UefiBootPage::addEntry);
    connect(m_removeButton, &QPushButton::clicked, this, &UefiBootPage::removeSelected);
    connect(m_requestMokButton, &QPushButton::clicked,
            this, &UefiBootPage::requestMokEnrollment);
    connect(m_cancelMokButton, &QPushButton::clicked,
            this, &UefiBootPage::cancelPendingMok);

    connect(m_service, &BootService::snapshotReady,
            this, [this](const BootSnapshot &) { updateSnapshot(); });
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
                if (success && action == QLatin1String("efi-batch"))
                    m_pending.clear();
                if (success)
                    KMessageBox::information(this, message, i18n("Boot Repair"));
                else
                    KMessageBox::error(this, message, i18n("Boot Repair Failed"));
            });

    updateSnapshot();
}

void UefiBootPage::updateStatus()
{
    const BootSnapshot &snapshot = m_service->snapshot();
    if (!snapshot.uefi)
        m_statusLabel->setText(i18n("This system was not booted in UEFI mode; firmware changes are unavailable."));
    else if (!m_hasEfiBootManager)
        m_statusLabel->setText(i18n("efibootmgr is missing; install it to manage UEFI firmware entries."));
    else
        m_statusLabel->setText(i18nd("miryu-toolkit", "UEFI firmware and Secure Boot state loaded."));
}

void UefiBootPage::setToolAvailabilityForTest(bool efiBootManager, bool mokUtil)
{
    m_toolOverride = true;
    m_hasEfiBootManager = efiBootManager;
    m_hasMokUtil = mokUtil;
    updateSnapshot();
}

void UefiBootPage::updateSnapshot()
{
    if (!m_toolOverride) {
        m_hasEfiBootManager = toolAvailable(QStringLiteral("efibootmgr"));
        m_hasMokUtil = toolAvailable(QStringLiteral("mokutil"));
    }
    const BootSnapshot &snapshot = m_service->snapshot();
    const QString previousSelection = selectedEntry()
        ? selectedEntry()->number : QString();

    m_updating = true;
    m_table->setRowCount(0);
    QList<FirmwareEntry> ordered;
    QStringList added;
    for (const QString &number : displayOrder()) {
        const FirmwareEntry *entry = snapshot.entry(number);
        if (!entry || m_pending.removals.contains(number) || added.contains(entry->number))
            continue;
        FirmwareEntry copy = *entry;
        copy.label = displayLabel(*entry);
        copy.enabled = displayEnabled(*entry);
        ordered.append(copy);
        added.append(entry->number);
    }
    for (const auto &entry : snapshot.firmware) {
        if (added.contains(entry.number) || m_pending.removals.contains(entry.number))
            continue;
        FirmwareEntry copy = entry;
        copy.label = displayLabel(entry);
        copy.enabled = displayEnabled(entry);
        ordered.append(copy);
        added.append(entry.number);
    }

    const QString nextBoot = displayNext();
    int rowToSelect = -1;
    m_table->setRowCount(ordered.size());
    for (int row = 0; row < ordered.size(); ++row) {
        const FirmwareEntry &entry = ordered.at(row);
        QStringList state{entry.enabled ? i18n("Enabled") : i18n("Disabled")};
        if (entry.current || entry.number == snapshot.bootCurrent)
            state.append(i18n("Current"));
        if (!nextBoot.isEmpty() && entry.number == nextBoot)
            state.append(i18n("Next boot"));
        if (m_pending.toggles.contains(entry.number) || m_pending.renames.contains(entry.number))
            state.append(i18n("Pending"));
        const QStringList values{entry.number, entry.label,
                                 entry.partitionUuid, state.join(i18n(", "))};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values.at(column));
            if (column == 0) {
                item->setData(NumberRole, entry.number);
                item->setData(IdentityRole, entry.identity());
                item->setData(InOrderRole, displayOrder().contains(entry.number));
            }
            m_table->setItem(row, column, item);
        }
        if (!previousSelection.isEmpty() && entry.number == previousSelection)
            rowToSelect = row;
    }
    for (const auto &addition : m_pending.additions) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const QStringList values{QStringLiteral("—"), addition.second, QString(),
                                 i18n("Pending add")};
        for (int column = 0; column < values.size(); ++column)
            m_table->setItem(row, column, new QTableWidgetItem(values.at(column)));
    }
    if (rowToSelect >= 0)
        m_table->selectRow(rowToSelect);

    m_secureBootValue->setText(securityText(snapshot.secureBoot));
    m_setupModeValue->setText(securityText(snapshot.setupMode));
    m_moduleSignatureValue->setText(securityText(snapshot.moduleSigEnforce));

    m_mokTree->clear();
    auto *enrolled = new QTreeWidgetItem(m_mokTree,
        {i18n("Enrolled MOK certificates (%1)", snapshot.enrolledCertificates.size())});
    for (const auto &certificate : snapshot.enrolledCertificates) {
        auto *item = new QTreeWidgetItem(enrolled, {certificateName(certificate), certificate.issuer,
                                                   certificate.notAfter, certificate.fingerprint});
        for (int column = 0; column < item->columnCount(); ++column)
            item->setToolTip(column, wrappedToolTip(item->text(column), m_mokTree->fontMetrics()));
    }
    auto *pending = new QTreeWidgetItem(m_mokTree,
        {i18n("Pending MOK confirmation (%1)", snapshot.pendingCertificates.size())});
    for (const auto &certificate : snapshot.pendingCertificates) {
        auto *item = new QTreeWidgetItem(pending, {certificateName(certificate), certificate.issuer,
                                                  certificate.notAfter, certificate.fingerprint});
        for (int column = 0; column < item->columnCount(); ++column)
            item->setToolTip(column, wrappedToolTip(item->text(column), m_mokTree->fontMetrics()));
    }
    enrolled->setFirstColumnSpanned(false);
    pending->setFirstColumnSpanned(false);
    m_mokTree->expandAll();

    QStringList rawSections;
    if (!snapshot.enrolledMok.trimmed().isEmpty())
        rawSections.append(i18n("Enrolled MOK output:") + QLatin1Char('\n')
                           + snapshot.enrolledMok.trimmed());
    if (!snapshot.pendingMok.trimmed().isEmpty())
        rawSections.append(i18n("Pending MOK output:") + QLatin1Char('\n')
                           + snapshot.pendingMok.trimmed());
    m_rawMok->setPlainText(rawSections.isEmpty()
        ? i18n("No raw MOK output was reported.") : rawSections.join(QStringLiteral("\n\n")));

    QStringList notices = snapshot.messages;
    notices.append(snapshot.securityMessages);
    if (snapshot.uefi && !m_hasEfiBootManager)
        notices.append(i18n("Missing tool: efibootmgr"));
    if (snapshot.uefi && !m_hasMokUtil)
        notices.append(i18n("Missing tool: mokutil"));
    if (snapshot.uefi && snapshot.secureBoot == BootSecurityState::Disabled)
        notices.append(i18nd("miryu-toolkit", "Secure Boot is disabled; MOK management is unavailable."));
    notices.removeAll(QString());
    m_noticeLabel->setText(notices.join(QLatin1Char('\n')));
    m_noticeLabel->setVisible(!notices.isEmpty());
    m_updating = false;

    updateStatus();
    updateControls();
}

const FirmwareEntry *UefiBootPage::selectedEntry() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0))
        return nullptr;
    return m_service->snapshot().entry(m_table->item(row, 0)->data(NumberRole).toString());
}

int UefiBootPage::selectedOrderPosition() const
{
    const FirmwareEntry *entry = selectedEntry();
    return entry ? displayOrder().indexOf(entry->number) : -1;
}

void UefiBootPage::updateControls()
{
    const BootSnapshot &snapshot = m_service->snapshot();
    const FirmwareEntry *entry = selectedEntry();
    const bool idle = !m_service->isBusy();
    const bool firmwareReady = idle && snapshot.uefi && m_hasEfiBootManager;
    const bool selected = firmwareReady && entry;
    const int orderPosition = selectedOrderPosition();
    const QStringList order = displayOrder();

    m_refreshButton->setEnabled(idle);
    m_moveUpButton->setEnabled(selected && orderPosition > 0);
    m_moveDownButton->setEnabled(selected && orderPosition >= 0
                                 && orderPosition + 1 < order.size());
    m_onceButton->setEnabled(selected);
    m_clearNextButton->setEnabled(firmwareReady && !displayNext().isEmpty());
    m_toggleButton->setEnabled(selected);
    m_toggleButton->setText(entry && displayEnabled(*entry)
        ? i18n("Disable") : i18n("Enable"));
    m_renameButton->setEnabled(selected);
    m_addButton->setEnabled(firmwareReady && !snapshot.espMounts.isEmpty()
                            && m_pending.additions.isEmpty());
    m_removeButton->setEnabled(selected && !entry->current
                               && entry->number != snapshot.bootCurrent);

    const int changes = pendingChangeCount();
    m_pendingLabel->setVisible(changes > 0);
    if (changes > 0) {
        m_pendingLabel->setText(i18n("Unsaved changes: %1. Nothing is written to the firmware until you save.",
                                     changes));
    }
    m_saveButton->setEnabled(firmwareReady && changes > 0);
    m_discardButton->setEnabled(changes > 0);

    const bool mokAvailable = idle && snapshot.uefi
        && snapshot.secureBoot != BootSecurityState::Disabled
        && snapshot.secureBoot != BootSecurityState::Unsupported;
    m_mokArea->setEnabled(mokAvailable);
    m_requestMokButton->setEnabled(mokAvailable && m_hasMokUtil);
    m_cancelMokButton->setEnabled(mokAvailable && m_hasMokUtil
        && (!snapshot.pendingCertificates.isEmpty() || !snapshot.pendingMok.trimmed().isEmpty()));
}

bool UefiBootPage::PendingEdits::isEmpty() const
{
    return !orderChanged && !nextChanged && toggles.isEmpty() && renames.isEmpty()
        && removals.isEmpty() && additions.isEmpty();
}

void UefiBootPage::PendingEdits::clear()
{
    *this = PendingEdits();
}

QStringList UefiBootPage::displayOrder() const
{
    return m_pending.orderChanged ? m_pending.order : m_service->snapshot().bootOrder;
}

bool UefiBootPage::displayEnabled(const FirmwareEntry &entry) const
{
    return m_pending.toggles.value(entry.number, entry.enabled);
}

QString UefiBootPage::displayLabel(const FirmwareEntry &entry) const
{
    return m_pending.renames.value(entry.number, entry.label);
}

QString UefiBootPage::displayNext() const
{
    return m_pending.nextChanged ? m_pending.next : m_service->snapshot().bootNext;
}

int UefiBootPage::pendingChangeCount() const
{
    return (m_pending.orderChanged ? 1 : 0)
        + (m_pending.nextChanged ? 1 : 0)
        + m_pending.toggles.size()
        + m_pending.renames.size()
        + m_pending.removals.size()
        + m_pending.additions.size();
}

QJsonArray UefiBootPage::pendingSteps(QString *summary) const
{
    const BootSnapshot &snapshot = m_service->snapshot();
    QJsonArray steps;
    QStringList lines;
    QMap<QString, FirmwareEntry> simulated;
    for (const auto &entry : snapshot.firmware)
        simulated.insert(entry.number, entry);

    const auto appendStep = [&steps](const QString &action, const QJsonObject &request) {
        steps.append(QJsonObject{{QStringLiteral("action"), action},
                                 {QStringLiteral("request"), request}});
    };

    if (m_pending.nextChanged) {
        appendStep(QStringLiteral("efi-next"),
            QJsonObject{{QStringLiteral("expectedNext"), snapshot.bootNext},
                        {QStringLiteral("number"), m_pending.next}});
        lines.append(m_pending.next.isEmpty()
            ? i18n("Clear the one-time next-boot entry")
            : i18n("Boot entry %1 once on the next startup", m_pending.next));
    }

    for (auto it = m_pending.renames.constBegin(); it != m_pending.renames.constEnd(); ++it) {
        auto entry = simulated.find(it.key());
        if (entry == simulated.end())
            continue;
        appendStep(QStringLiteral("efi-rename"),
            QJsonObject{{QStringLiteral("number"), it.key()},
                        {QStringLiteral("label"), it.value()},
                        {QStringLiteral("expectedIdentity"), entry->identity()}});
        lines.append(i18n("Rename entry %1: “%2” → “%3”", it.key(), entry->label, it.value()));
        entry->label = it.value();
    }

    for (auto it = m_pending.toggles.constBegin(); it != m_pending.toggles.constEnd(); ++it) {
        auto entry = simulated.find(it.key());
        if (entry == simulated.end())
            continue;
        appendStep(QStringLiteral("efi-toggle"),
            QJsonObject{{QStringLiteral("number"), it.key()},
                        {QStringLiteral("enabled"), it.value()},
                        {QStringLiteral("expectedIdentity"), entry->identity()}});
        lines.append(it.value() ? i18n("Enable entry %1 (%2)", it.key(), entry->label)
                                : i18n("Disable entry %1 (%2)", it.key(), entry->label));
        entry->enabled = it.value();
    }

    for (const QString &number : m_pending.removals) {
        auto entry = simulated.find(number);
        if (entry == simulated.end())
            continue;
        appendStep(QStringLiteral("efi-remove"),
            QJsonObject{{QStringLiteral("number"), number},
                        {QStringLiteral("expectedIdentity"), entry->identity()}});
        lines.append(i18n("Remove entry %1 (%2)", number, entry->label));
        simulated.erase(entry);
    }

    QStringList effectiveOrder = m_pending.orderChanged ? m_pending.order : snapshot.bootOrder;
    for (const QString &number : m_pending.removals)
        effectiveOrder.removeAll(number);
    QStringList expectedOrder = snapshot.bootOrder;
    for (const QString &number : m_pending.removals)
        expectedOrder.removeAll(number);
    if (m_pending.orderChanged && effectiveOrder != expectedOrder) {
        appendStep(QStringLiteral("efi-order"),
            QJsonObject{{QStringLiteral("order"), QJsonArray::fromStringList(effectiveOrder)},
                        {QStringLiteral("expectedOrder"), QJsonArray::fromStringList(expectedOrder)}});
        lines.append(i18n("Change the boot order to %1", effectiveOrder.join(QStringLiteral(", "))));
    }

    for (const auto &addition : m_pending.additions) {
        appendStep(QStringLiteral("efi-add"),
            QJsonObject{{QStringLiteral("path"), addition.first},
                        {QStringLiteral("label"), addition.second},
                        {QStringLiteral("expectedOrder"), QJsonArray::fromStringList(effectiveOrder)}});
        lines.append(i18n("Add “%1” for %2", addition.second, addition.first));
    }

    if (summary) {
        *summary = lines.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
            + i18n("Apply these changes to the UEFI firmware now?");
    }
    return steps;
}

void UefiBootPage::markDirty()
{
    updateSnapshot();
}

void UefiBootPage::saveChanges()
{
    QString summary;
    const QJsonArray steps = pendingSteps(&summary);
    if (steps.isEmpty())
        return;
    if (KMessageBox::questionTwoActions(this, summary, i18n("Confirm Boot Changes"),
                                        KGuiItem(i18n("Yes")), KGuiItem(i18n("No")))
        != KMessageBox::PrimaryAction) {
        return;
    }
    m_ownedAction = QStringLiteral("efi-batch");
    if (!m_service->apply(QStringLiteral("efi-batch"),
                          QJsonObject{{QStringLiteral("steps"), steps}},
                          i18n("Apply UEFI firmware boot entry changes"))) {
        m_ownedAction.clear();
    }
}

void UefiBootPage::discardChanges()
{
    m_pending.clear();
    updateSnapshot();
}

void UefiBootPage::submit(const QString &action, const QJsonObject &request,
                          const QString &description, const QString &confirmation)
{
    if (KMessageBox::questionTwoActions(this, confirmation, i18n("Confirm Boot Change"),
                                        KGuiItem(i18n("Yes")), KGuiItem(i18n("No")))
        != KMessageBox::PrimaryAction) {
        return;
    }
    m_ownedAction = action;
    if (!m_service->apply(action, request, description))
        m_ownedAction.clear();
}

void UefiBootPage::moveSelected(int offset)
{
    const FirmwareEntry *entry = selectedEntry();
    if (!entry)
        return;
    QStringList order = displayOrder();
    const int position = order.indexOf(entry->number);
    const int destination = position + offset;
    if (position < 0 || destination < 0 || destination >= order.size())
        return;
    order.swapItemsAt(position, destination);
    m_pending.orderChanged = order != m_service->snapshot().bootOrder;
    m_pending.order = order;
    markDirty();
}

void UefiBootPage::setNextBoot()
{
    const FirmwareEntry *entry = selectedEntry();
    if (!entry)
        return;
    m_pending.nextChanged = true;
    m_pending.next = entry->number;
    markDirty();
}

void UefiBootPage::clearNextBoot()
{
    m_pending.nextChanged = true;
    m_pending.next.clear();
    markDirty();
}

void UefiBootPage::toggleSelected()
{
    const FirmwareEntry *entry = selectedEntry();
    if (!entry || !m_service->snapshot().entry(entry->number))
        return;
    const bool enabled = !displayEnabled(*entry);
    if (enabled == m_service->snapshot().entry(entry->number)->enabled)
        m_pending.toggles.remove(entry->number);
    else
        m_pending.toggles.insert(entry->number, enabled);
    markDirty();
}

void UefiBootPage::renameSelected()
{
    const FirmwareEntry *entry = selectedEntry();
    if (!entry)
        return;
    bool accepted = false;
    const QString label = QInputDialog::getText(this, i18n("Rename EFI Entry"),
        i18n("New firmware entry label:"), QLineEdit::Normal,
        displayLabel(*entry), &accepted).trimmed();
    if (!accepted)
        return;
    if (!validLabel(label)) {
        KMessageBox::error(this,
            i18n("The label must contain 1 to 80 printable characters on a single line."),
            i18n("Invalid Label"));
        return;
    }
    if (label == entry->label)
        m_pending.renames.remove(entry->number);
    else
        m_pending.renames.insert(entry->number, label);
    markDirty();
}

void UefiBootPage::addEntry()
{
    const BootSnapshot &snapshot = m_service->snapshot();
    if (snapshot.espMounts.isEmpty())
        return;
    QString start = snapshot.espMounts.constFirst().path + QStringLiteral("/EFI");
    if (!QDir(start).exists())
        start = snapshot.espMounts.constFirst().path;
    const QString path = QFileDialog::getOpenFileName(this,
        i18n("Select an EFI Loader"), start,
        i18n("EFI programs (*.efi *.EFI);;All files (*)"));
    if (path.isEmpty())
        return;
    bool accepted = false;
    QString defaultLabel = QFileInfo(path).completeBaseName();
    if (defaultLabel.isEmpty())
        defaultLabel = i18n("Fedora");
    const QString label = QInputDialog::getText(this, i18n("Add EFI Entry"),
        i18n("Firmware entry label:"), QLineEdit::Normal, defaultLabel, &accepted).trimmed();
    if (!accepted)
        return;

    QStringList arguments;
    QString error;
    if (!BootProbe::firmwareAddArguments(path, label, snapshot.espMounts,
                                         &arguments, &error)) {
        KMessageBox::error(this,
            i18n("The selected loader cannot be registered:\n%1", error),
            i18n("Cannot Add EFI Entry"));
        return;
    }
    m_pending.additions.append({path, label});
    markDirty();
}

void UefiBootPage::removeSelected()
{
    const FirmwareEntry *entry = selectedEntry();
    if (!entry || entry->current || entry->number == m_service->snapshot().bootCurrent)
        return;
    if (!m_pending.removals.contains(entry->number))
        m_pending.removals.append(entry->number);
    m_pending.renames.remove(entry->number);
    m_pending.toggles.remove(entry->number);
    if (displayNext() == entry->number) {
        m_pending.nextChanged = true;
        m_pending.next.clear();
    }
    if (m_pending.orderChanged)
        m_pending.order.removeAll(entry->number);
    markDirty();
}

void UefiBootPage::requestMokEnrollment()
{
    const QString path = QFileDialog::getOpenFileName(this,
        i18n("Select a DER Certificate"), QDir::homePath(),
        i18n("DER certificates (*.der *.crt *.cer);;All files (*)"));
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() < 64 || info.size() > 64 * 1024) {
        KMessageBox::error(this,
            i18n("Choose a regular DER certificate file between 64 bytes and 64 KiB."),
            i18n("Invalid Certificate"));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        KMessageBox::error(this,
            i18n("Could not read the selected DER certificate."),
            i18n("Certificate Unavailable"));
        return;
    }
    QByteArray certificate = file.read(64 * 1024 + 1);
    QString error;
    if (certificate.size() < 64 || certificate.size() > 64 * 1024
        || quint8(certificate.at(0)) != 0x30
        || !BootProbe::validateDerCertificate(certificate, &error)) {
        if (error.isEmpty())
            error = i18n("The file is not a supported ASN.1 DER certificate.");
        KMessageBox::error(this, error, i18n("Invalid Certificate"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(i18n("MOK Enrollment Password"));
    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *passwordForm = new QFormLayout;
    auto *passwordEdit = new QLineEdit(&dialog);
    passwordEdit->setObjectName(QStringLiteral("mokPassword"));
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setMaxLength(256);
    auto *confirmationEdit = new QLineEdit(&dialog);
    confirmationEdit->setObjectName(QStringLiteral("mokPasswordConfirmation"));
    confirmationEdit->setEchoMode(QLineEdit::Password);
    confirmationEdit->setMaxLength(256);
    passwordForm->addRow(i18n("Password:"), passwordEdit);
    passwordForm->addRow(i18n("Repeat password:"), confirmationEdit);
    dialogLayout->addLayout(passwordForm);
    auto *passwordHint = new QLabel(
        i18n("Use 1 to 256 printable characters. Firmware will ask for this password to confirm enrollment."), &dialog);
    passwordHint->setWordWrap(true);
    dialogLayout->addWidget(passwordHint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(i18n("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(i18n("Cancel"));
    dialogLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString password = passwordEdit->text();
    QString repeatedPassword = confirmationEdit->text();
    const auto clearPasswords = qScopeGuard([&password, &repeatedPassword]() {
        password.fill(QLatin1Char('\0'));
        repeatedPassword.fill(QLatin1Char('\0'));
    });
    passwordEdit->clear();
    confirmationEdit->clear();
    if (password != repeatedPassword) {
        KMessageBox::error(this, i18n("The two MOK passwords do not match."),
                             i18n("Password Mismatch"));
        return;
    }
    bool validPassword = !password.isEmpty() && password.size() <= 256
        && password.toUtf8().size() <= 1024;
    for (const QChar character : password)
        validPassword = validPassword && character.isPrint();
    if (!validPassword) {
        KMessageBox::error(this,
            i18n("The MOK password must contain 1 to 256 printable characters."),
            i18n("Invalid Password"));
        return;
    }

    QJsonObject request{
        {QStringLiteral("certificate"), QString::fromLatin1(certificate.toBase64())},
        {QStringLiteral("password"), password}
    };
    submit(QStringLiteral("mok-import"), request,
           i18n("Request MOK certificate enrollment"),
           i18n("Request enrollment of this certificate?\n%1\n\nAfter reboot, confirm the request in the MOK manager. The password is sent only to the privileged helper and is not shown in the log.", path));
    request.remove(QStringLiteral("password"));
}

void UefiBootPage::cancelPendingMok()
{
    submit(QStringLiteral("mok-revoke"), QJsonObject{},
           i18n("Cancel pending MOK enrollment"),
           i18n("Cancel all pending MOK enrollment requests? Already enrolled certificates will not be removed."));
}
