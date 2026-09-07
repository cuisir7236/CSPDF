#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;
class QAction;

/**
 * M5 托盘图标：左键弹出/收起面板，右键菜单控制播放/翻页/退出
 */
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);

    void setPlaying(bool playing);
    void setVisible(bool visible);

signals:
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void panelToggled();
    void quitRequested();

private:
    QSystemTrayIcon m_tray;
    QMenu *m_menu = nullptr;
    QAction *m_playAction = nullptr;
};
