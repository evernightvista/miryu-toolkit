#pragma once

#include <QEnterEvent>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QMouseEvent>
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

struct TransparencyLevel
{
    QString id;
    QString name;
    QString description;
    int blurStrength = 7;
    int noiseStrength = 14;
    QString imageResource;
};

class SelectableImageCard : public QFrame
{
    Q_OBJECT

public:
    explicit SelectableImageCard(const QString &imagePath, const QString &title,
                                 const QString &description, QWidget *parent = nullptr);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

Q_SIGNALS:
    void clicked();
    void hovered();
    void unhovered();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_selected = false;
    QLabel *m_imageLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    void updateStyle();
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

    void collectSystemLogs();
    void cleanupOldKernel();
    void updateSystem();
    void startPrivilegedSystemUpdate(QDialog *logDialog, QPlainTextEdit *logView,
                                     QDialogButtonBox *closeButtons);
    void listFailedServices();
    void unlockRpmDatabase();
    void viewDnf5Log();
    void cleanupUnusedPackages();
    void viewCrashInfo();
    static QString currentUserName();

    QWidget *buildPersonalizationTab();
    void setupTransparencyLevels();
    void selectTransparencyCard(int index);
    void applyTransparencyLevel();
    int detectCurrentTransparencyLevel() const;

    QTabWidget *m_tabs = nullptr;
    QVector<ExtraComponent> m_components;
    QMap<QString, ComponentWidgets> m_componentWidgets;
    QProcess *m_runningProcess = nullptr;

    QListWidget *m_environmentList = nullptr;
    QLabel *m_environmentStatus = nullptr;
    QLabel *m_bottomStatus = nullptr;

    QVector<TransparencyLevel> m_transparencyLevels;
    QVector<SelectableImageCard *> m_transparencyCards;
    int m_selectedTransparencyIndex = 1;
    QLabel *m_transparencyDescription = nullptr;
};
