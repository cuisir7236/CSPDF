#include "ttsengine.h"
#include "docmanager.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>
#include <chrono>
#include <thread>

// 连续空页达到该数量则自动暂停（防扫描版整本空翻）
static constexpr int kMaxBlankPages = 5;

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent) {
}

TtsEngine::~TtsEngine() {
    shutdown();
}

bool TtsEngine::init() {
    // ---- 确定 espeak-ng 数据目录（优先级：打包清洗版 > 系统多架构目录） ----
    // 评审 #3：不再硬编码 /usr/lib/x86_64-linux-gnu
    QString dataParent;
    const QString cleaned = QStringLiteral("/usr/share/cspdfreader/espeak-ng-data");
    if (QFile::exists(cleaned + QStringLiteral("/voices/!v/f5"))) {
        dataParent = QStringLiteral("/usr/share/cspdfreader/");
    } else {
        // 编译期注入的多架构目录（CMake 由 CMAKE_LIBRARY_ARCHITECTURE 生成）
#ifdef ESPEAK_DATA_DIR
        const QString sysData = QStringLiteral(ESPEAK_DATA_DIR "/espeak-ng-data");
        if (QFile::exists(sysData))
            dataParent = sysData.left(sysData.size() - QStringLiteral("espeak-ng-data").size());
#endif
        // 运行期兜底：常见发行版多架构路径（arm64/loong64/riscv64/x86_64）
        if (dataParent.isEmpty()) {
            const QStringList fallbacks = {
                QStringLiteral("/usr/lib/x86_64-linux-gnu/espeak-ng-data"),
                QStringLiteral("/usr/lib/aarch64-linux-gnu/espeak-ng-data"),
                QStringLiteral("/usr/lib/loongarch64-linux-gnu/espeak-ng-data"),
                QStringLiteral("/usr/lib/riscv64-linux-gnu/espeak-ng-data"),
            };
            for (const QString &p : fallbacks) {
                if (QFile::exists(p)) {
                    dataParent = p.left(p.size() - QStringLiteral("espeak-ng-data").size());
                    break;
                }
            }
        }
    }
    if (dataParent.isEmpty()) {
        qWarning() << "espeak-ng-data 未找到（请安装 espeak-ng-data 包）";
        return false;
    }

    // ---- 检测 Piper 模型（优先系统安装路径） ----
    QString model;
    QString config;
    const QString sysPiper = QStringLiteral("/usr/share/cspdfreader/piper/");
    const QString userPiper = QDir::homePath()
        + QStringLiteral("/.local/share/cspdfreader/piper/");
    if (QFile::exists(sysPiper + QStringLiteral("zh_CN-huayan-medium.onnx"))) {
        model = sysPiper + QStringLiteral("zh_CN-huayan-medium.onnx");
        config = model + QStringLiteral(".json");
    } else if (QFile::exists(userPiper + QStringLiteral("zh_CN-huayan-medium.onnx"))) {
        model = userPiper + QStringLiteral("zh_CN-huayan-medium.onnx");
        config = model + QStringLiteral(".json");
    }
    m_piperAvailable = !model.isEmpty() && QFile::exists(config);
    if (m_piperAvailable) {
        m_piperModel = model;
        m_piperConfig = config;
        qInfo().noquote() << "Piper TTS 可用:" << model;
        // 启动常驻合成服务专用线程（QProcess 仅在该线程内创建与访问）
        m_piperThread = std::thread(&TtsEngine::piperLoop, this);
    } else {
        qInfo().noquote() << "Piper 模型缺失，回退 espeak-ng";
    }

    const QByteArray dataParentBytes = dataParent.toLocal8Bit();
    const int rc = espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 0,
                                     dataParentBytes.constData(), 0);
    if (rc < 0) {
        qWarning() << "espeak_Initialize failed:" << rc;
        return false;
    }
    // 音色：默认少女音（cmn+f5）；失败回退普通话
    if (espeak_SetVoiceByName("cmn+f5") != EE_OK) {
        espeak_VOICE voice;
        memset(&voice, 0, sizeof(voice));
        voice.languages = "cmn";
        espeak_SetVoiceByProperties(&voice);
    }
    espeak_SetParameter(espeakPITCH, 70, 0);
    setRate(Rate::Normal);
    return true;
}
void TtsEngine::shutdown() {
    if (m_shutdownDone.exchange(true))
        return;   // 幂等：aboutToQuit 与析构只执行一次

    m_stop = true;
    m_playing = false;
    wakeWorker();

    // 停止 piper 专用线程：清空队列 + 置终止标志。
    // 常驻 QProcess 的 kill/回收由 piper 线程在自身上下文内完成（评审 #4）。
    {
        std::lock_guard<std::mutex> lk(m_piperQueueMutex);
        m_piperStop = true;
        m_piperJobs.clear();
    }
    m_piperCv.notify_all();
    if (m_piperThread.joinable())
        m_piperThread.join();

    // 回收 worker 线程（aplay 进程的清理发生在 worker 线程内部，不再跨线程触碰）
    if (m_worker.joinable()) {
        // 等待工作线程退出（最多 2 秒），防止阻塞在 Synchronize 导致退不出
        for (int i = 0; i < 40 && m_running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (m_worker.joinable())
            m_worker.detach();   // 超时兜底：分离，进程继续退出
    }
    espeak_Terminate();
}

void TtsEngine::play() {
    if (!m_doc || m_doc->pageCount() <= 0)
        return;
    if (m_playing.load())
        return;

    m_playing = true;
    emit playingChanged(true);
    if (!m_running.load()) {
        m_running = true;
        m_worker = std::thread(&TtsEngine::workerLoop, this);
    } else {
        // 线程已存在但处于暂停：唤醒循环
        restartFromCurrent();
        wakeWorker();
    }
}

void TtsEngine::pause() {
    if (!m_playing.load())
        return;
    m_playing = false;
    // 不再直接操作 m_playProc / espeak API：worker 线程检测到 m_playing=false
    // 后自行回收 aplay 进程、取消 espeak 合成（同线程安全）
    wakeWorker();
    emit playingChanged(false);
}

void TtsEngine::stop() {
    m_playing = false;
    wakeWorker();
    emit playingChanged(false);
}

void TtsEngine::nextPage() {
    if (!m_doc)
        return;
    if (m_doc->nextPage())
        restartFromCurrent();
}

void TtsEngine::prevPage() {
    if (!m_doc)
        return;
    if (m_doc->prevPage())
        restartFromCurrent();
}

void TtsEngine::setPage(int page) {
    if (!m_doc)
        return;
    if (m_doc->setPage(page))
        restartFromCurrent();
}

void TtsEngine::setRate(Rate rate) {
    m_rate = rate;
    switch (rate) {
    case Rate::Slow:   m_wpm.store(120); break;
    case Rate::Normal: m_wpm.store(175); break;
    case Rate::Fast:   m_wpm.store(240); break;
    }
    // 注意：espeak_SetParameter 改在 worker 线程合成前调用（GUI 线程不触碰 espeak API）
}

void TtsEngine::applyCurrentPage() {
    m_playing = false;
    wakeWorker();
    emit playingChanged(false);
}

void TtsEngine::restartFromCurrent() {
    // 翻页/跳页：仅置位并唤醒；espeak 取消由 worker 线程在块间自行处理，
    // GUI 线程不触碰 espeak API（避免任何潜在阻塞/并发问题）
    wakeWorker();
}

void TtsEngine::wakeWorker() {
    m_waitCv.notify_all();
}

bool TtsEngine::playWav(const QString &wav) {
    // 仅 worker 线程调用：确保上次播放进程已回收
    if (m_playProc) {
        m_playProc->kill();
        m_playProc->waitForFinished(500);
        delete m_playProc;
        m_playProc = nullptr;
    }
    m_playProc = new QProcess;
    m_playProc->setProgram(QStringLiteral("aplay"));
    m_playProc->setArguments({wav});
    m_playProc->start();
    if (!m_playProc->waitForStarted(3000)) {
        delete m_playProc;
        m_playProc = nullptr;
        return false;
    }
    // 等待播放完成或被暂停/退出中断（100ms 轮询，CPU 占用低）。
    // m_playing 由主线程原子置位，worker 线程据此安全退出等待。
    while (m_playing.load() && m_playProc) {
        if (m_playProc->state() == QProcess::NotRunning)
            break;
        m_playProc->waitForFinished(100);
    }
    reapPlaybackProc();
    return true;
}

void TtsEngine::reapPlaybackProc() {
    // 仅 worker 线程调用：同线程回收 aplay 进程，避免跨线程 QProcess 访问
    if (m_playProc) {
        if (m_playProc->state() != QProcess::NotRunning) {
            m_playProc->kill();
            m_playProc->waitForFinished(500);
        }
        delete m_playProc;
        m_playProc = nullptr;
    }
}
// ============ Piper 专用线程：同线程管理常驻合成服务 QProcess ============

void TtsEngine::ensurePiperServer() {
    // 仅 piper 线程调用（评审 #4：QProcess 只在其创建线程内访问）
    if (m_piperServer && m_piperServer->state() == QProcess::Running)
        return;
    if (m_piperServer) {
        m_piperServer->kill();
        m_piperServer->waitForFinished(1000);
        delete m_piperServer;
        m_piperServer = nullptr;
    }
    // 启动常驻服务：python3 piper_server.py <model> <config>
    QString server = QStringLiteral("/usr/share/cspdfreader/piper_server.py");
    if (!QFile::exists(server))
        server = QDir::homePath()
            + QStringLiteral("/.local/share/cspdfreader/piper_server.py");
    if (!QFile::exists(server)) {
        qWarning() << "piper_server.py 未找到，Piper 合成不可用";
        return;
    }
    m_piperServer = new QProcess;
    m_piperServer->setProgram(QStringLiteral("python3"));
    m_piperServer->setArguments({server, m_piperModel, m_piperConfig});
    m_piperServer->start();
    if (!m_piperServer->waitForStarted(5000))
        return;
    // 等待 "ready" 响应（模型预加载完成）
    QByteArray buf;
    for (int i = 0; i < 200 && m_piperServer->state() == QProcess::Running
                        && !m_piperStop; ++i) {
        m_piperServer->waitForReadyRead(100);
        buf += m_piperServer->readAllStandardOutput();
        if (buf.contains("\"ready\""))
            return;
    }
    // 启动失败/超时：同线程清理
    m_piperServer->kill();
    m_piperServer->waitForFinished(500);
    delete m_piperServer;
    m_piperServer = nullptr;
}

bool TtsEngine::synthesizeOnPiperThread(const QString &text, const QString &wav) {
    // 仅 piper 线程调用
    ensurePiperServer();
    if (!m_piperServer)
        return false;

    // 向常驻服务发送合成请求（JSON），等待 WAV 就绪
    const QByteArray req = QJsonDocument(QJsonObject{{QStringLiteral("text"), text},
                                                     {QStringLiteral("wav"), wav}})
                               .toJson(QJsonDocument::Compact) + "\n";
    m_piperServer->write(req);
    m_piperServer->waitForBytesWritten(3000);
    // 按行读取响应（整页合成可能需 10-60s，超时放宽到 120s）
    QByteArray resp;
    bool gotLine = false;
    for (int i = 0; i < 1200 && m_piperServer->state() == QProcess::Running
                        && !m_piperStop; ++i) {
        if (!m_piperServer->waitForReadyRead(100))
            continue;
        resp += m_piperServer->readAllStandardOutput();
        if (resp.contains('\n')) {
            gotLine = true;
            break;
        }
        if (resp.contains("\"error\""))
            break;
    }
    if (!gotLine || resp.contains("\"error\"") || !resp.contains("\"wav\""))
        return false;
    return QFile::exists(wav);
}

void TtsEngine::enqueuePiperJob(PiperJob &&job) {
    {
        std::lock_guard<std::mutex> lk(m_piperQueueMutex);
        if (m_piperStop)
            return;   // 已进入关闭流程，丢弃新任务
        m_piperJobs.push_back(std::move(job));
    }
    m_piperCv.notify_one();
}

bool TtsEngine::synthesizeToFile(const QString &text, const QString &wav) {
    // 任意线程（worker 实时合成）：投递同步任务并等待结果
    if (!m_piperThread.joinable())
        return false;
    std::promise<bool> prom;
    std::future<bool> fut = prom.get_future();
    enqueuePiperJob({text, wav, false, std::move(prom)});
    while (fut.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (m_stop.load() || !m_playing.load())
            return false;   // 退出/暂停流程：不再等待（piper 线程侧已捕获 broken_promise）
    }
    try {
        return fut.get();
    } catch (...) {
        return false;   // promise 在关闭时被销毁等异常一律视为合成失败
    }
}

void TtsEngine::preSynthesize(const QString &text, const QString &wav) {
    // 任意线程（worker 流水线预合成）：异步投递，成功后由 piper 线程写缓存
    if (!m_piperThread.joinable())
        return;
    {
        std::lock_guard<std::mutex> lk(m_piperQueueMutex);
        if (m_piperStop)
            return;
        m_piperJobs.push_back({text, wav, true, std::promise<bool>()}); // C++17 聚合初始化
    }
    m_piperCv.notify_one();
}

void TtsEngine::piperLoop() {
    while (true) {
        PiperJob job;
        {
            std::unique_lock<std::mutex> lk(m_piperQueueMutex);
            m_piperCv.wait(lk, [this] { return m_piperStop || !m_piperJobs.empty(); });
            if (m_piperStop) {
                if (m_piperJobs.empty())
                    break;                       // 终止：退出并清理常驻进程
                job = std::move(m_piperJobs.front());
                m_piperJobs.pop_front();
                if (!job.async) {
                    try { job.result.set_value(false); } catch (...) {}
                }
                continue;                        // 丢弃剩余任务（调用方均已按 m_stop 退出）
            }
            job = std::move(m_piperJobs.front());
            m_piperJobs.pop_front();
        }

        const bool ok = synthesizeOnPiperThread(job.text, job.wav);
        if (job.async) {
            // 预合成成功：写入缓存（与 worker 线程读缓存互斥）
            if (ok) {
                std::lock_guard<std::mutex> lk(m_cacheMutex);
                m_cacheWav = job.wav;
                m_haveCache = true;
            }
        } else {
            try { job.result.set_value(ok); } catch (...) { /* future 已销毁 */ }
        }
    }
    // 同线程回收常驻合成服务进程
    if (m_piperServer) {
        m_piperServer->kill();
        m_piperServer->waitForFinished(1000);
        delete m_piperServer;
        m_piperServer = nullptr;
    }
}
void TtsEngine::workerLoop() {
    int blankStreak = 0;   // 连续空页计数

    while (!m_stop.load()) {
        // 暂停时休眠，等待 play/翻页/退出唤醒（替代 30ms 忙等）
        {
            std::unique_lock<std::mutex> lk(m_waitMutex);
            m_waitCv.wait_for(lk, std::chrono::milliseconds(250),
                              [this] { return m_playing.load() || m_stop.load(); });
        }
        if (m_stop.load())
            break;
        if (!m_playing.load())
            continue;

        const int page = m_doc ? m_doc->currentPage() : 0;
        const int total = m_doc ? m_doc->pageCount() : 0;
        if (page < 1 || page > total) {
            m_playing = false;
            emit playingChanged(false);
            continue;
        }

        emit pageStarted(page);

        const QString text = m_doc ? m_doc->extractPageText(page) : QString();
        if (text.trimmed().isEmpty()) {
            // 连续空页过多（扫描版）自动暂停，避免整本空翻
            if (++blankStreak >= kMaxBlankPages) {
                emit errorOccurred(QStringLiteral("连续 %1 页无可朗读文字（扫描版？），已暂停")
                                       .arg(blankStreak));
                m_playing = false;
                emit playingChanged(false);
                blankStreak = 0;
                continue;
            }
            if (m_playing.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                if (page < total)
                    m_doc->nextPage();
                else {
                    m_playing = false;
                    emit playingChanged(false);
                }
            }
            continue;
        }
        blankStreak = 0;

        bool spoken = false;
        if (m_piperAvailable) {
            // 当前页音频：优先用预合成缓存，否则实时合成
            QString wav;
            {
                std::lock_guard<std::mutex> lk(m_cacheMutex);
                if (m_haveCache && QFile::exists(m_cacheWav)) {
                    wav = m_cacheWav;
                    m_haveCache = false;
                }
            }
            if (wav.isEmpty()) {
                wav = QDir::tempPath() + QStringLiteral("/cspdf_page.wav");
                if (!synthesizeToFile(text, wav))
                    wav.clear();
            }
            if (!wav.isEmpty()) {
                // 播放当前页的同时，后台流水线预合成下一页（无缝衔接）。
                // 评审 #4：不再 new 线程直接碰 QProcess，改为投递到 piper 专用线程，
                // 合成成功后由该线程加锁写入缓存，worker 不阻塞等待。
                if (m_playing.load() && page < total) {
                    const QString nextText = m_doc->extractPageText(page + 1);
                    if (!nextText.trimmed().isEmpty()) {
                        const QString nextWav = QDir::tempPath() + QStringLiteral("/cspdf_next.wav");
                        preSynthesize(nextText, nextWav);
                    }
                }
                spoken = playWav(wav);
            }
        }
        if (!spoken && m_playing.load()) {
            // espeak 兜底：分块合成+播放（每块约 1~2 秒），块间检查暂停/停止，
            // 取消动作由 worker 线程执行（GUI 线程不触碰 espeak API，杜绝界面冻结）
            const QStringList chunks = splitEspeakChunks(text);
            bool interrupted = false;
            for (const QString &chunk : chunks) {
                if (!m_playing.load() || m_stop.load()) {
                    interrupted = true;
                    break;
                }
                espeak_SetParameter(espeakRATE, m_wpm.load(), 0);   // worker 线程应用语速
                const QByteArray utf8 = chunk.toUtf8();
                espeak_Synth(utf8.constData(), utf8.size() + 1, 0, POS_CHARACTER, 0,
                             espeakCHARS_UTF8, nullptr, nullptr);
                espeak_Synchronize();   // 等待本块播放完成（块小，暂停响应延迟有界）
                if (!m_playing.load() || m_stop.load()) {
                    interrupted = true;
                    break;
                }
            }
            if (interrupted)
                espeak_Cancel();   // 两次 espeak_Synth 调用之间调用，同线程安全
        }

        emit pageFinished(page);

        if (!m_playing.load()) {
            continue;
        }

        if (page < total) {
            m_doc->nextPage();
        } else {
            m_playing = false;
            emit playingChanged(false);
        }
    }
    m_running = false;
}

QStringList TtsEngine::splitEspeakChunks(const QString &text) const {
    // 将整页文本切分为小段（默认约 80 字符/段，优先在句末断句），
    // 使暂停/翻页/停止的响应延迟有界（约 1~2 秒），避免整页长时间合成不可打断。
    QStringList chunks;
    static const int maxLen = 80;
    int pos = 0;
    const int n = text.size();
    while (pos < n) {
        int end = qMin(pos + maxLen, n);
        if (end < n) {
            // 向前找最近的句子结束符（中文句末标点及英文 .!?; 与换行符）
            for (int i = end; i > pos; --i) {
                const QChar c = text.at(i - 1);
                if (c == u'。' || c == u'！' || c == u'？' || c == u'；' || c == u'…'
                    || c == u'.' || c == u'!' || c == u'?' || c == u';' || c == u'\n') {
                    end = i;
                    break;
                }
            }
        }
        QString piece = text.mid(pos, end - pos).trimmed();
        if (!piece.isEmpty())
            chunks << piece;
        pos = end;
        while (pos < n && text.at(pos).isSpace())
            ++pos;
    }
    return chunks;
}
