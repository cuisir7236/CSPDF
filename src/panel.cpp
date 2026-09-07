#include "panel.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

Panel::Panel(QWidget *parent)
    : QWidget(parent) {
    setupUi();
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(300);
}

void Panel::setupUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(4);

    // ---- 标题行：文档名 + 退出按钮 ----
    auto *topRow = new QHBoxLayout;
    m_docLabel = new QLabel(QStringLiteral("未检测到文档"), this);
    m_docLabel->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px;"));
    m_quitBtn = new QPushButton(QStringLiteral("✕"), this);
    m_quitBtn->setFixedSize(28, 24);
    m_quitBtn->setToolTip(QStringLiteral("退出程序"));
    m_quitBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:#E81123; border:none;"
        " border-radius:4px; font-size:16px; font-weight:bold; }"
        " QPushButton:hover { background:#E81123; color:white; }"
        " QPushButton:pressed { background:#B23A3A; color:white; }"));
    topRow->addWidget(m_docLabel, 1);
    topRow->addWidget(m_quitBtn);
    root->addLayout(topRow);

    // ---- 页码行：页码文本 + 进度条 ----
    auto *pageRow = new QHBoxLayout;
    m_pageLabel = new QLabel(QStringLiteral("第 - / - 页"), this);
    m_pageLabel->setStyleSheet(QStringLiteral("font-size:12px; color:#555;"));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_progress, 1);
    root->addLayout(pageRow);

    // ---- 控制区：两行等宽，严格左右对齐 ----
    // 按钮宽 58，间距 6；页码框 = 上页+播放 = 58*2+6 = 122；语速 = 58
    const int bw = 58;

    m_prevBtn = new QPushButton(QStringLiteral("上页"), this);
    m_playBtn = new QPushButton(QStringLiteral("播放"), this);
    m_nextBtn = new QPushButton(QStringLiteral("下页"), this);
    m_prevBtn->setFixedSize(bw, 28);
    m_playBtn->setFixedSize(bw, 28);
    m_nextBtn->setFixedSize(bw, 28);

    const QString btnStyle = QStringLiteral(
        "QPushButton { background:#3D6BE0; color:white; border:none;"
        " border-radius:5px; font-size:11px; font-weight:bold; }"
        " QPushButton:hover { background:#5279E8; }"
        " QPushButton:pressed { background:#2F55C0; }");
    m_prevBtn->setStyleSheet(btnStyle);
    m_playBtn->setStyleSheet(btnStyle);
    m_nextBtn->setStyleSheet(btnStyle);

    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setFixedSize(bw * 2 + 6, 28);   // = 上页+播放整体宽度

    m_rateCombo = new QComboBox(this);
    m_rateCombo->addItems({QStringLiteral("慢速"), QStringLiteral("中速"), QStringLiteral("快速")});
    m_rateCombo->setCurrentIndex(1);
    m_rateCombo->setFixedSize(bw, 28);          // 与下页同宽

    const QString inputStyle = QStringLiteral(
        "QSpinBox, QComboBox { background:white; color:#1E1E1E;"
        " border:1px solid #B8C0CC; border-radius:4px; padding:0 4px;"
        " font-size:11px; }"
        " QSpinBox:focus, QComboBox:focus { border-color:#3D6BE0; }"
        " QSpinBox::up-button, QSpinBox::down-button {"
        "   width:0px; border:none; background:transparent; }");
    m_pageSpin->setStyleSheet(inputStyle);
    m_rateCombo->setStyleSheet(inputStyle);

    // 行3：上页 | 播放 | 下页
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);
    btnRow->addStretch();
    btnRow->addWidget(m_prevBtn);
    btnRow->addWidget(m_playBtn);
    btnRow->addWidget(m_nextBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 行4：页码框(2列宽) | 语速 —— 总宽 = 行3
    auto *utilRow = new QHBoxLayout;
    utilRow->setSpacing(6);
    utilRow->addStretch();
    utilRow->addWidget(m_pageSpin);
    utilRow->addWidget(m_rateCombo);
    utilRow->addStretch();
    root->addLayout(utilRow);

    connect(m_quitBtn, &QPushButton::clicked, this, &Panel::quitRequested);
    connect(m_prevBtn, &QPushButton::clicked, this, &Panel::prevClicked);
    connect(m_playBtn, &QPushButton::clicked, this, &Panel::playPauseClicked);
    connect(m_nextBtn, &QPushButton::clicked, this, &Panel::nextClicked);
    connect(m_rateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { if (!m_updating) emit rateChanged(idx); });
    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) { if (!m_updating) emit pageSelected(v); });
}

void Panel::setDocumentInfo(const QString &title, int pageCount, int currentPage) {
    m_updating = true;
    m_docLabel->setText(title);
    m_pageSpin->setRange(1, pageCount);
    m_updating = false;
    setPageInfo(currentPage, pageCount);
}

void Panel::setPageInfo(int page, int pageCount) {
    m_updating = true;
    m_pageLabel->setText(QStringLiteral("第 %1 / %2 页").arg(page).arg(pageCount));
    m_pageSpin->setValue(page);
    m_progress->setValue(pageCount > 0 ? page * 100 / pageCount : 0);
    m_updating = false;
}

void Panel::setPlaying(bool playing) {
    m_playBtn->setText(playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void Panel::clearDocument() {
    m_updating = true;
    m_docLabel->setText(QStringLiteral("未检测到文档"));
    m_pageLabel->setText(QStringLiteral("第 - / - 页"));
    m_pageSpin->setRange(1, 1);
    m_progress->setValue(0);
    m_updating = false;
}

void Panel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(238);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect(), 16, 16);
}

void Panel::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    static bool positioned = false;
    if (!positioned) {
        positioned = true;
        QSettings s(QStringLiteral("CSPDFREADER"), QStringLiteral("panel"));
        if (s.contains(QStringLiteral("pos")))
            move(s.value(QStringLiteral("pos")).toPoint());
        else
            moveToBottomRight();
    }
}

void Panel::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void Panel::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging)
        move(event->globalPosition().toPoint() - m_dragOffset);
}

void Panel::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        savePosition();
    }
}

void Panel::moveToBottomRight() {
    if (QScreen *scr = QApplication::primaryScreen()) {
        const QRect avail = scr->availableGeometry();
        move(avail.right() - width() - 24, avail.bottom() - height() - 48);
    }
}

void Panel::savePosition() {
    QSettings s(QStringLiteral("CSPDFREADER"), QStringLiteral("panel"));
    s.setValue(QStringLiteral("pos"), pos());
}
