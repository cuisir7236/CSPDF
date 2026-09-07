#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <mutex>

namespace Poppler {
class Document;
}

/**
 * 文档状态管理：持有 Poppler 文档、总页数、当前页码，
 * 并从 deepin-reader 的 user.db 读取上次阅读位置作为初始页码。
 */
class DocManager : public QObject {
    Q_OBJECT
public:
    explicit DocManager(QObject *parent = nullptr);
    ~DocManager() override;

    bool setDocument(const QString &path);
    void clearDocument();

    QString path() const { std::lock_guard<std::mutex> lk(m_mutex); return m_path; }
    QString title() const { std::lock_guard<std::mutex> lk(m_mutex); return m_title; }
    int pageCount() const { std::lock_guard<std::mutex> lk(m_mutex); return m_pageCount; }
    int currentPage() const { std::lock_guard<std::mutex> lk(m_mutex); return m_currentPage; }

    QString extractPageText(int page);  // 线程安全，1-based

    bool setPage(int page);
    bool nextPage();
    bool prevPage();

signals:
    void documentLoaded(const QString &path, const QString &title,
                        int pageCount, int startPage);
    void pageChanged(int page, int pageCount);
    void documentCleared();

private:
    int loadLastPageFromUserDb(const QString &path);
    void openPoppler(const QString &path);

    mutable std::mutex m_mutex;
    std::unique_ptr<Poppler::Document> m_doc;
    QString m_path;
    QString m_title;
    int m_pageCount = 0;
    int m_currentPage = 1;
};
