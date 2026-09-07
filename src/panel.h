#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QComboBox;
class QProgressBar;

/**
 * 控制面板：右下角悬浮圆角卡片
 * 标题行(文档名+退出按钮) / 页码+进度 / 控制条(上页·播放·下页·页码框·语速)
 */
class Panel : public QWidget {
    Q_OBJECT
public:
    explicit Panel(QWidget *parent = nullptr);

    void setDocumentInfo(const QString &title, int pageCount, int currentPage);
    void setPageInfo(int page, int pageCount);
    void setPlaying(bool playing);
    void clearDocument();

signals:
    void quitRequested();      // 右上角退出按钮
    void playPauseClicked();
    void prevClicked();
    void nextClicked();
    void pageSelected(int page);
    void rateChanged(int index);   // 0慢 1中 2快

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void setupUi();
    void moveToBottomRight();
    void savePosition();

    QLabel *m_docLabel = nullptr;
    QPushButton *m_quitBtn = nullptr;
    QLabel *m_pageLabel = nullptr;
    QPushButton *m_prevBtn = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QSpinBox *m_pageSpin = nullptr;
    QComboBox *m_rateCombo = nullptr;
    QProgressBar *m_progress = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
    bool m_updating = false;
};
