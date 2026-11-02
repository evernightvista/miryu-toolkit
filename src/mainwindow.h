#pragma once

#include "grubconfig.h"

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDialog;
class QDialogButtonBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSpinBox;
class QTextEdit;

struct ExtraComponent
{
    QString id;
    QString name;
    QString description;
    QString helperExecutable;
    QString removeHelperExecutable;
    QStringList packages;
};

struct ComponentWidgets
{
    QLabel *statusLabel = nullptr;
    QPushButton *installButton = nullptr;
    QPushButton *uninstallButton = nullptr;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWidget *buildSystemAssistantTab();
    QWidget *buildExtrasTab();
    QWidget *buildEnvironmentTab();
    QWidget *buildAboutTab();
    QWidget *createComponentCard(const ExtraComponent &component);

    void setupComponents();
    void refreshComponentStates();
    void refreshOneComponent(const ExtraComponent &component);
    bool packageInstalled(const QString &packageName) const;
    void runPackageOperation(const ExtraComponent &component, const QString &operation);
    void setComponentsBusy(bool busy);

    void loadEnvironmentVariables();
    void addEnvironmentVariable();
    void editEnvironmentVariable();
    void removeEnvironmentVariable();
    void applyEnvironmentVariables();
    void restoreDefaultEnvironmentVariables();
    void setDefaultEnvironmentVariables();
    bool validateEnvironmentLine(const QString &line, QString *errorText = nullptr) const;

    // ── Sleep tab ──
    QWidget *buildSleepTab();
    void loadSleepConfig();
    void saveSleepConfig();
    void refreshSleepChecks();
    void updateSleepDependents();
    void updateHibernateMethodSensitivity();
    void fixSleepProblems();

    static QString readSysFile(const QString &path);
    static QString readKernelConfig();
    static bool kernelConfigEnabled(const QString &configText, const QString &key);

    void collectSystemLogs();
    void cleanupOldKernel();
    void updateSystem();
    void startPrivilegedSystemUpdate(QDialog *logDialog, QPlainTextEdit *logView);
    void listFailedServices();
    void unlockRpmDatabase();
    void viewDnf5Log();
    void cleanupUnusedPackages();
    void viewCrashInfo();
    static QString currentUserName();

    // ── Boot Menu (GRUB2) tab ──
    QWidget *buildBootMenuTab();
    QWidget *buildBootMenuSection();
    void loadGrubConfig();
    void updateGrubUiFromConfig();
    void onSaveGrubClicked();
    void onAddGrubParam();
    void onRemoveGrubParam();
    void onEditGrubParam();
    void onMoveGrubUp();
    void onMoveGrubDown();
    void refreshGrubActionButtons();
    QStringList grubParameterList() const;
    void setGrubParameterList(const QStringList &params);
    bool validateGrubParameter(const QString &param, int ignoredRow = -1);
    QString resolveGrubHelperPath() const;
    void setGrubBusy(bool busy);

    QTabWidget *m_tabs = nullptr;
    QVector<ExtraComponent> m_components;
    QMap<QString, ComponentWidgets> m_componentWidgets;
    QProcess *m_runningProcess = nullptr;

    QListWidget *m_environmentList = nullptr;
    QLabel *m_environmentStatus = nullptr;
    QLabel *m_bottomStatus = nullptr;

    // Sleep tab members
    QCheckBox *m_sleepSuspendCheck = nullptr;
    QCheckBox *m_sleepHibernateCheck = nullptr;
    QCheckBox *m_sleepSuspendThenHibernateCheck = nullptr;
    QCheckBox *m_sleepHybridSleepCheck = nullptr;
    QComboBox *m_suspendModeCombo = nullptr;
    QComboBox *m_hibernateMethodCombo = nullptr;
    QComboBox *m_hibernateCompressorCombo = nullptr;
    QListWidget *m_sleepProblemsList = nullptr;
    QListWidget *m_sleepAvailableList = nullptr;
    QLabel *m_sleepStatusLabel = nullptr;
    QPushButton *m_sleepApplyButton = nullptr;
    QPushButton *m_sleepFixButton = nullptr;

    // GRUB Boot Menu members
    QPointer<GrubConfig> m_grubConfig;
    QCheckBox *m_grubShowMenuCheck = nullptr;
    QSpinBox *m_grubDelaySpin = nullptr;
    QCheckBox *m_grubRememberCheck = nullptr;
    QTextEdit *m_grubCurrentParamsDisplay = nullptr;
    QListWidget *m_grubParamListWidget = nullptr;
    QLineEdit *m_grubNewParamEdit = nullptr;
    QPushButton *m_grubAddParamButton = nullptr;
    QPushButton *m_grubRemoveParamButton = nullptr;
    QPushButton *m_grubEditParamButton = nullptr;
    QPushButton *m_grubMoveUpButton = nullptr;
    QPushButton *m_grubMoveDownButton = nullptr;
    QLabel *m_grubStatusLabel = nullptr;
    QPushButton *m_grubSaveButton = nullptr;
};
