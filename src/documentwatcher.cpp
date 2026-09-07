#include "documentwatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <unistd.h>
#include <algorithm>

DocumentWatcher::DocumentWatcher(QObject *parent)
    : QObject(parent) {
    m_timer.setInterval(1000);
    m_timer.setTimerType(Qt::CoarseTimer);   // 1s 轮询无需高精度，省电
    connect(&m_timer, &QTimer::timeout, this, &DocumentWatcher::poll);
    m_timer.start();
}

void DocumentWatcher::poll() {
    const QStringList pids = findReaderPids();
    QString current;

    for (const QString &pid : pids) {
        const QStringList pdfs = findOpenPdfs(pid);
        for (const QString &p : pdfs) {
            // 优先取位于用户目录/挂载盘的（排除 /proc、/sys 等伪路径）
            if (p.startsWith(QLatin1String("/home")) ||
                p.startsWith(QLatin1String("/media")) ||
                p.startsWith(QLatin1String("/run/host"))) {
                current = normalizePath(p);
                break;
            }
        }
        if (!current.isEmpty())
            break;
    }

    if (!current.isEmpty() && current != m_current) {
        m_current = current;
        emit documentChanged(m_current);
    } else if (current.isEmpty() && !m_current.isEmpty()) {
        m_current.clear();
        emit documentClosed();
    }
}

QStringList DocumentWatcher::findReaderPids() {
    QStringList result;
    const QDir proc(QStringLiteral("/proc"));
    if (!proc.exists())
        return result;
    const QStringList dirs =
        proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &d : dirs) {
        bool ok = false;
        d.toInt(&ok);
        if (!ok)
            continue;

        QFile commFile(QStringLiteral("/proc/%1/comm").arg(d));
        if (!commFile.open(QIODevice::ReadOnly))
            continue;   // 其他用户进程无权读取，跳过
        const QString comm =
            QString::fromUtf8(commFile.readAll()).trimmed();
        if (comm == QLatin1String("deepin-reader"))
            result << d;
    }
    return result;
}

QStringList DocumentWatcher::findOpenPdfs(const QString &pid) {
    QStringList result;
    const QString fdDirPath = QStringLiteral("/proc/%1/fd").arg(pid);
    const QDir fdDir(fdDirPath);
    if (!fdDir.exists())
        return result;
    const QStringList fds =
        fdDir.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);

    for (const QString &fd : fds) {
        char buf[4096];
        const QString link = fdDir.absoluteFilePath(fd);
        const ssize_t n = readlink(link.toLocal8Bit().constData(),
                                   buf, sizeof(buf) - 1);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        const QString target = QString::fromLocal8Bit(buf);
        if (target.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
            result << target;
    }
    return result;
}

QString DocumentWatcher::normalizePath(const QString &path) {
    // 玲珑容器内路径可能带有 /run/host/rootfs 前缀，剥离之以便与 user.db 匹配
    const QString prefix = QStringLiteral("/run/host/rootfs");
    if (path.startsWith(prefix))
        return path.mid(prefix.size());
    return path;
}
