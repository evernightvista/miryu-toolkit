#ifndef GRUBCONFIG_H
#define GRUBCONFIG_H

#include <QObject>
#include <QString>

class GrubConfig : public QObject
{
    Q_OBJECT

public:
    explicit GrubConfig(QObject *parent = nullptr);

    bool load();

    bool showMenu() const { return m_showMenu; }
    void setShowMenu(bool show) { m_showMenu = show; }

    int timeout() const { return m_timeout; }
    void setTimeout(int seconds) { m_timeout = seconds; }

    bool rememberLast() const { return m_rememberLast; }
    void setRememberLast(bool remember) { m_rememberLast = remember; }

    QString bootParams() const { return m_bootParams; }
    void setBootParams(const QString &params) { m_bootParams = params; }

    QString currentCmdline() const { return m_currentCmdline; }
    QString generateConfigContent() const;

private:
    void parseConfigContent(const QString &content);
    void parseCurrentCmdline();
    void loadFromBls();
    QString escapedBootParams() const;

    bool m_showMenu = true;
    int m_timeout = 10;
    bool m_rememberLast = false;
    QString m_bootParams;
    QString m_currentCmdline;
};

#endif // GRUBCONFIG_H
