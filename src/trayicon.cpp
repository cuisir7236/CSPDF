#include "trayicon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QPolygonF>
#include <QPainter>
#include <QPixmap>

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent) {
    // 特殊标识图标：深蓝圆角方块 + 白色"读"字（不与其他程序图标混淆）
    // 特殊标识图标：深蓝圆角方块 + 白色大"读"字（清晰可辨，不与其它程序混淆）
    QPixmap pix(64, 64);
    pix.fill(Qt::transparent);
    {
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(30, 120, 230));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(2, 2, 60, 60, 16, 16);
        // 中央白色大"读"字
        p.setPen(QColor(255, 255, 255));
        QFont f(QStringLiteral("Noto Sans CJK SC"), 26);
        f.setPixelSize(40);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(2, 2, 60, 60),
                   Qt::AlignCenter, QStringLiteral("读"));
    }
    // 安装图标到 XDG 主题目录（供 DDE dock 按名称加载，解决 IconName 为空不显示问题）
    const QString iconName = QStringLiteral("cspdfreader");
    const QString iconDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/icons/hicolor/64x64/apps");
    QDir().mkpath(iconDir);
    const QString iconFile = iconDir + QLatin1Char('/') + iconName + QStringLiteral(".png");
    if (!QFile::exists(iconFile))
        pix.save(iconFile);

    // 优先按主题名加载（IconName 非空），失败则回退到内置像素图
    QIcon icon = QIcon::fromTheme(iconName, QIcon(pix));
    m_tray.setIcon(icon);
    m_tray.setToolTip(QStringLiteral("老崔PDF阅读工具"));

    m_menu = new QMenu;
    m_playAction = m_menu->addAction(QStringLiteral("▶ 播放"));
    QAction *prevAction = m_menu->addAction(QStringLiteral("⏮ 上一页"));
    QAction *nextAction = m_menu->addAction(QStringLiteral("⏭ 下一页"));
    m_menu->addSeparator();
    QAction *panelAction = m_menu->addAction(QStringLiteral("显示/隐藏面板"));
    m_menu->addSeparator();
    QAction *quitAction = m_menu->addAction(QStringLiteral("退出"));

    connect(m_playAction, &QAction::triggered, this, &TrayIcon::playPauseClicked);
    connect(prevAction, &QAction::triggered, this, &TrayIcon::prevClicked);
    connect(nextAction, &QAction::triggered, this, &TrayIcon::nextClicked);
    connect(panelAction, &QAction::triggered, this, &TrayIcon::panelToggled);
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray.setContextMenu(m_menu);
    connect(&m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger)
                    emit panelToggled();
            });
}

void TrayIcon::setPlaying(bool playing) {
    m_playAction->setText(playing ? QStringLiteral("⏸ 暂停") : QStringLiteral("▶ 播放"));
}

void TrayIcon::setVisible(bool visible) {
    m_tray.setVisible(visible);
}
