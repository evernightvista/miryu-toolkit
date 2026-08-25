/*
    SPDX-FileCopyrightText: 2026 Evernight Vista Team <13278297951@sina.cn>
    SPDX-License-Identifier: GPL-3.0-or-later
*/

// KDE System Settings module that provides a launcher for the
// external Miryu Toolkit application. It behaves like the built-in
// ExternalAppModule: the external app is automatically launched when
// the KCM is first shown, and a "Relaunch" button is provided.
// It is registered under the "System" section via the
// X-KDE-System-Settings-Parent-Category field in kcm_miryu_toolkit.json.

#include <KCModule>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KLocalizedString>

#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QProcess>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QTimer>

class MiryuToolkitKcm : public KCModule
{
    Q_OBJECT
public:
    MiryuToolkitKcm(QObject *parent, const KPluginMetaData &data)
        : KCModule(parent, data)
    {
        // 该 KCM 仅作为启动器，无可配置项，隐藏默认/重置/应用按钮
        setButtons(KCModule::NoAdditionalButton);

        auto *layout = new QVBoxLayout(widget());
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(12);

        auto *title = new QLabel(i18nd("miryu-toolkit", "Miryu Toolkit"), widget());
        QFont titleFont = title->font();
        titleFont.setPointSize(titleFont.pointSize() + 6);
        titleFont.setBold(true);
        title->setFont(titleFont);

        auto *description = new QLabel(
            i18nd("miryu-toolkit",
                  "Miryu Toolkit manages optional Miryu components (Wine, Steam, "
                  "MIDI playback, extra fonts), system-wide environment variables, "
                  "and collects system logs."),
            widget());
        description->setWordWrap(true);

        m_status = new QLabel(widget());
        m_status->setWordWrap(true);
        m_status->setStyleSheet(QStringLiteral("color: palette(text)"));

        m_launchButton = new QPushButton(
            QIcon::fromTheme(QStringLiteral("miryu-toolkit")),
            i18nd("miryu-toolkit", "Relaunch Miryu Toolkit"),
            widget());
        m_launchButton->setObjectName(QStringLiteral("launchMiryuToolkit"));

        connect(m_launchButton, &QPushButton::clicked, this, &MiryuToolkitKcm::launchToolkit);

        layout->addWidget(title);
        layout->addWidget(description);
        layout->addStretch(1);
        layout->addWidget(m_launchButton, 0, Qt::AlignLeft);
        layout->addSpacing(8);
        layout->addWidget(m_status);
        layout->addStretch(10);

        // 在 widget 首次显示时自动启动 Miryu Toolkit
        // 使用事件过滤器捕获 show 事件
        widget()->installEventFilter(this);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == widget() && event->type() == QEvent::Show && m_firstShow) {
            m_firstShow = false;
            // 延迟到事件循环空闲时启动，确保 widget 完全显示
            QTimer::singleShot(0, this, &MiryuToolkitKcm::launchToolkit);
        }
        return KCModule::eventFilter(watched, event);
    }

private Q_SLOTS:
    void launchToolkit()
    {
        const QString program = QStringLiteral("miryu-toolkit");
        qint64 pid = 0;
        if (QProcess::startDetached(program, {}, QString(), &pid)) {
            m_status->setText(
                i18nd("miryu-toolkit",
                      "Miryu Toolkit is an external application and has been "
                      "automatically launched."));
        } else {
            m_status->setText(
                i18nd("miryu-toolkit",
                      "Could not launch Miryu Toolkit. Make sure it is installed."));
        }
    }

private:
    QPushButton *m_launchButton = nullptr;
    QLabel *m_status = nullptr;
    bool m_firstShow = true;
};

K_PLUGIN_CLASS_WITH_JSON(MiryuToolkitKcm, "kcm_miryu_toolkit.json")

#include "kcm_miryu_toolkit.moc"
