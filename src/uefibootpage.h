#pragma once

#include <QMap>
#include <QPair>
#include <QStringList>
#include <QWidget>

class BootService;
struct FirmwareEntry;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QJsonArray;
class QJsonObject;

class UefiBootPage : public QWidget
{
    Q_OBJECT

public:
    explicit UefiBootPage(BootService *service, QWidget *parent = nullptr);

    // Lets tests describe the available command line tools instead of depending
    // on what happens to be installed on the test machine.
    void setToolAvailabilityForTest(bool efiBootManager, bool mokUtil);

private:
    struct PendingEdits {
        bool orderChanged = false;
        QStringList order;
        bool nextChanged = false;
        QString next;
        QMap<QString, bool> toggles;
        QMap<QString, QString> renames;
        QStringList removals;
        QList<QPair<QString, QString>> additions;

        bool isEmpty() const;
        void clear();
    };

    void updateSnapshot();
    void updateStatus();
    void updateControls();
    const FirmwareEntry *selectedEntry() const;
    int selectedOrderPosition() const;
    QStringList displayOrder() const;
    bool displayEnabled(const FirmwareEntry &entry) const;
    QString displayLabel(const FirmwareEntry &entry) const;
    QString displayNext() const;
    int pendingChangeCount() const;
    QJsonArray pendingSteps(QString *summary) const;
    void markDirty();
    void saveChanges();
    void discardChanges();
    void submit(const QString &action, const QJsonObject &request,
                const QString &description, const QString &confirmation);
    void moveSelected(int offset);
    void setNextBoot();
    void clearNextBoot();
    void toggleSelected();
    void renameSelected();
    void addEntry();
    void removeSelected();
    void requestMokEnrollment();
    void cancelPendingMok();

    BootService *m_service = nullptr;
    bool m_updating = false;
    bool m_hasEfiBootManager = false;
    bool m_hasMokUtil = false;
    bool m_toolOverride = false;
    QString m_ownedAction;
    PendingEdits m_pending;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_noticeLabel = nullptr;
    QLabel *m_pendingLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_discardButton = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_moveUpButton = nullptr;
    QPushButton *m_moveDownButton = nullptr;
    QPushButton *m_onceButton = nullptr;
    QPushButton *m_clearNextButton = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QPushButton *m_renameButton = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_removeButton = nullptr;

    QLabel *m_secureBootValue = nullptr;
    QLabel *m_setupModeValue = nullptr;
    QLabel *m_moduleSignatureValue = nullptr;
    QTreeWidget *m_mokTree = nullptr;
    QWidget *m_mokArea = nullptr;
    QGroupBox *m_rawGroup = nullptr;
    QPlainTextEdit *m_rawMok = nullptr;
    QPushButton *m_requestMokButton = nullptr;
    QPushButton *m_cancelMokButton = nullptr;
    QPlainTextEdit *m_output = nullptr;
};
