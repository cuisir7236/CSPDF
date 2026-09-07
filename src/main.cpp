#include <QApplication>
#include <QMessageBox>

#include "docmanager.h"
#include "documentwatcher.h"
#include "panel.h"
#include "trayicon.h"
#include "ttsengine.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CSPDFREADER"));
    app.setApplicationDisplayName(QStringLiteral("老崔PDF阅读工具"));
    app.setQuitOnLastWindowClosed(false);

    DocManager doc;
    DocumentWatcher watcher;
    TtsEngine tts;
    if (!tts.init()) {
        QMessageBox::critical(nullptr, QStringLiteral("老崔PDF阅读工具"),
                              QStringLiteral("语音引擎初始化失败（espeak-ng 不可用）"));
        return 1;
    }
    tts.setDocManager(&doc);

    Panel panel;
    TrayIcon tray;

    // 文档识别 -> 文档管理
    QObject::connect(&watcher, &DocumentWatcher::documentChanged,
                     &doc, &DocManager::setDocument);
    QObject::connect(&watcher, &DocumentWatcher::documentClosed,
                     &doc, &DocManager::clearDocument);

    // 文档加载 -> 面板/朗读复位
    QObject::connect(&doc, &DocManager::documentLoaded,
                     &panel, [&panel](const QString &, const QString &title,
                                      int total, int page) {
                         panel.setDocumentInfo(title, total, page);
                     });
    QObject::connect(&doc, &DocManager::pageChanged,
                     &panel, &Panel::setPageInfo);
    QObject::connect(&doc, &DocManager::documentCleared,
                     &panel, &Panel::clearDocument);
    QObject::connect(&doc, &DocManager::documentLoaded,
                     &tts, &TtsEngine::applyCurrentPage);

    // 面板/托盘控制 -> 朗读
    QObject::connect(&panel, &Panel::playPauseClicked, &tts, [&tts]() {
        if (tts.isPlaying()) tts.pause(); else tts.play();
    });
    QObject::connect(&panel, &Panel::prevClicked, &tts, &TtsEngine::prevPage);
    QObject::connect(&panel, &Panel::nextClicked, &tts, &TtsEngine::nextPage);
    QObject::connect(&panel, &Panel::pageSelected, &tts, &TtsEngine::setPage);
    QObject::connect(&panel, &Panel::rateChanged, &tts, [&tts](int idx) {
        tts.setRate(static_cast<TtsEngine::Rate>(idx));
    });
    QObject::connect(&tray, &TrayIcon::playPauseClicked, &tts, [&tts]() {
        if (tts.isPlaying()) tts.pause(); else tts.play();
    });
    QObject::connect(&tray, &TrayIcon::prevClicked, &tts, &TtsEngine::prevPage);
    QObject::connect(&tray, &TrayIcon::nextClicked, &tts, &TtsEngine::nextPage);

    // 播放状态 -> 面板/托盘
    // 要求：运行时窗口一直存在 —— 暂停/停止时若面板被隐藏则自动重新显示并置顶，
    // 保证任何时候都有可操作的窗口；播放中保持当前可见状态。
    QObject::connect(&tts, &TtsEngine::playingChanged,
                     &panel, [&panel](bool playing) {
                         panel.setPlaying(playing);
                         if (!playing && !panel.isVisible()) {
                             panel.show();
                             panel.raise();
                             panel.activateWindow();
                         }
                     });
    QObject::connect(&tts, &TtsEngine::playingChanged,
                     &tray, &TrayIcon::setPlaying);
    QObject::connect(&tts, &TtsEngine::errorOccurred,
                     &panel, [&panel](const QString &msg) {
                         panel.setWindowTitle(msg);
                     });

    // 托盘面板切换
    QObject::connect(&tray, &TrayIcon::panelToggled, &panel, [&panel]() {
        if (panel.isVisible()) {
            panel.hide();
        } else {
            panel.show();
            panel.raise();
            panel.activateWindow();
        }
    });

    // 退出：面板右上角 ✕ / 托盘菜单
    QObject::connect(&panel, &Panel::quitRequested, &app, &QApplication::quit);
    QObject::connect(&tray, &TrayIcon::quitRequested, &app, &QApplication::quit);

    // 退出前确保停止朗读（espeak_Cancel + 线程回收）
    QObject::connect(&app, &QApplication::aboutToQuit, &tts, &TtsEngine::shutdown);

    tray.setVisible(true);
    panel.show();

    return app.exec();
}
