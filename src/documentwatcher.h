#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>

/**
 * M2 文档识别模块：
 * 轮询 /proc 找到玲珑版 deepin-reader（内层进程 comm=deepin-reader），
 * 扫描 /proc/<pid>/fd 中指向 *.pdf 的符号链接，获得当前打开文档的绝对路径。
 */
class DocumentWatcher : public QObject {
    Q_OBJECT
public:
    explicit DocumentWatcher(QObject *parent = nullptr);
    QString currentPath() const { return m_current; }

signals:
    void documentChanged(const QString &path);
    void documentClosed();

private slots:
    void poll();

private:
    static QStringList findReaderPids();
    static QStringList findOpenPdfs(const QString &pid);
    static QString normalizePath(const QString &path);

    QTimer m_timer;
    QString m_current;
};
