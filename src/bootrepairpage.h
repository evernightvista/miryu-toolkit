#pragma once

#include <QWidget>

class BootService;
struct BackupInfo;
struct RepairTarget;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

class BootRepairPage : public QWidget
{
    Q_OBJECT

public:
    explicit BootRepairPage(BootService *service, QWidget *parent = nullptr);

private:
    void updateSnapshot();
    void updateBackups();
    void updateStatus();
    void updateFirmwareMode();
    void targetChanged(bool preserveEsp = false);
    void rebuildEsp(const QString &preferredKey);
    void rebuildDisks();
    void espChanged();
    void updateDiagnostics();
    void updateBackupDetails();
    void updateControls();
    RepairTarget currentTarget() const;
    QString firmwareMode() const;
    const BackupInfo *selectedBackup() const;
    QJsonObject targetRequest(const RepairTarget &target) const;
    void submit(const QString &action, const QJsonObject &request,
                const QString &description, const QString &confirmation);
    void repair();
    void restore();

    BootService *m_service = nullptr;
    bool m_updating = false;
    QString m_ownedAction;

    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QGroupBox *m_repairGroup = nullptr;
    QFormLayout *m_form = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_target = nullptr;
    QLabel *m_subvolumeLabel = nullptr;
    QLineEdit *m_subvolume = nullptr;
    QComboBox *m_disk = nullptr;
    QComboBox *m_esp = nullptr;
    QComboBox *m_espRelative = nullptr;
    QLineEdit *m_bootloaderId = nullptr;
    QCheckBox *m_register = nullptr;
    QCheckBox *m_fallback = nullptr;
    QLabel *m_architectureLabel = nullptr;
    QLabel *m_architecture = nullptr;
    QPlainTextEdit *m_diagnostics = nullptr;
    QPushButton *m_repairButton = nullptr;

    QTreeWidget *m_backupTree = nullptr;
    QPlainTextEdit *m_backupDetails = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPlainTextEdit *m_output = nullptr;
};
