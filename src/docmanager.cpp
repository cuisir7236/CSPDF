#include "docmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include <poppler-qt6.h>

DocManager::DocManager(QObject *parent)
    : QObject(parent) {
}

DocManager::~DocManager() = default;

bool DocManager::setDocument(const QString &path) {
    std::lock_guard<std::mutex> lk(m_mutex);
    openPoppler(path);
    if (!m_doc || m_doc->isLocked()) {
        m_doc.reset();
        m_path.clear();
        m_title.clear();
        m_pageCount = 0;
        m_currentPage = 1;
        return false;
    }
    m_path = path;
    m_title = QFileInfo(path).fileName();
    m_pageCount = m_doc->numPages();
    m_currentPage = loadLastPageFromUserDb(path);
    if (m_currentPage < 1)
        m_currentPage = 1;
    if (m_currentPage > m_pageCount)
        m_currentPage = m_pageCount;
    emit documentLoaded(m_path, m_title, m_pageCount, m_currentPage);
    emit pageChanged(m_currentPage, m_pageCount);
    return true;
}

void DocManager::clearDocument() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_doc.reset();
        m_path.clear();
        m_title.clear();
        m_pageCount = 0;
        m_currentPage = 1;
    }
    emit documentCleared();
}

QString DocManager::extractPageText(int page) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_doc || page < 1 || page > m_pageCount)
        return QString();
    std::unique_ptr<Poppler::Page> p(m_doc->page(page - 1));
    if (!p)
        return QString();
    return p->text(QRectF()).trimmed();
}

bool DocManager::setPage(int page) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (page < 1 || page > m_pageCount || page == m_currentPage)
        return false;
    m_currentPage = page;
    emit pageChanged(m_currentPage, m_pageCount);
    return true;
}

bool DocManager::nextPage() {
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_currentPage >= m_pageCount)
        return false;
    const int next = m_currentPage + 1;
    lk.unlock();           // 先释放再调用 setPage（避免重入）
    return setPage(next);
}

bool DocManager::prevPage() {
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_currentPage <= 1)
        return false;
    const int prev = m_currentPage - 1;
    lk.unlock();
    return setPage(prev);
}

int DocManager::loadLastPageFromUserDb(const QString &path) {
    const QString dbPath = QDir::homePath() +
        QStringLiteral("/.local/share/deepin/deepin-reader/user.db");
    const QString connName = QStringLiteral("cspdfreader_userdb");
    int page = 1;
    if (QSqlDatabase::contains(connName))
        QSqlDatabase::removeDatabase(connName);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            qWarning() << "user.db 打开失败:" << db.lastError().text();
            return page;
        }
        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "SELECT currentPage FROM operation WHERE filePath = ?"));
        query.addBindValue(path);
        if (query.exec() && query.next())
            page = query.value(0).toInt();
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    return page;
}

void DocManager::openPoppler(const QString &path) {
    m_doc = Poppler::Document::load(path);
}
