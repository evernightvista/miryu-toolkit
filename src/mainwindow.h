#pragma once

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QVector>

class QDialog;
class QDialogButtonBox;
class QPlainTextEdit;

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

    void collectSystemLogs();
    void cleanupOldKernel();
    void updateSystem();
    void startPrivilegedSystemUpdate(QDialog *logDialog, QPlainTextEdit *logView,
                                     QDialogButtonBox *closeButtons);
    static QString currentUserName();

    QTabWidget *m_tabs = nullptr;
    QVector<ExtraComponent> m_components;
    QMap<QString, ComponentWidgets> m_componentWidgets;
    QProcess *m_runningProcess = nullptr;

    QListWidget *m_environmentList = nullptr;
    QLabel *m_environmentStatus = nullptr;
    QLabel *m_bottomStatus = nullptr;
};
