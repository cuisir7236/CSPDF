#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

class QProcess;

#include <espeak-ng/speak_lib.h>

class DocManager;

/**
 * M3 朗读引擎：espeak-ng 逐页合成朗读（Piper 神经语音优先，缺失自动降级）。
 * 工作线程循环：取当前页文本 -> 合成朗读 -> 推进页码。
 *
 * 线程安全设计（评审 #4 修复）：
 *  - m_playProc（aplay 播放进程）仅由 worker 线程创建/读写/回收；暂停/停止/退出
 *    只置位标志并唤醒 worker，由 worker 线程在自身上下文内 kill+delete，
 *    杜绝主线程与 worker 线程跨线程访问同一 QProcess。
 *  - m_piperServer（常驻 Piper 合成服务 QProcess）仅由专用 piper 线程创建/读写/
 *    回收；worker 的实时合成与流水线预合成请求统一经互斥队列投递到该线程串行
 *    执行（同线程管理 + 队列加锁），不再有 worker/预合成线程同时触碰同一 QProcess。
 *  - 预合成缓存（piper 线程写 / worker 线程读）由 m_cacheMutex 保护。
 */
class TtsEngine : public QObject {
    Q_OBJECT
public:
    enum class Rate { Slow, Normal, Fast };

    explicit TtsEngine(QObject *parent = nullptr);
    ~TtsEngine() override;

    bool init();
    void shutdown();
    void setDocManager(DocManager *dm) { m_doc = dm; }

    void play();
    void pause();
    void stop();
    void nextPage();
    void prevPage();
    void setPage(int page);
    void setRate(Rate rate);
    void applyCurrentPage();   // 文档切换后重设工作页

    bool isPlaying() const { return m_playing.load(); }
    Rate rate() const { return m_rate; }

signals:
    void playingChanged(bool playing);
    void pageStarted(int page);
    void pageFinished(int page);
    void errorOccurred(const QString &message);

private:
    // ---- Piper 合成任务（投递到 piper 专用线程串行执行） ----
    struct PiperJob {
        QString text;
        QString wav;
        bool async = false;          // true: 成功后由 piper 线程直接写入预合成缓存
        std::promise<bool> result;   // 仅同步任务使用
    };

    void workerLoop();
    void piperLoop();
    QStringList splitEspeakChunks(const QString &text) const;  // espeak 分块（限暂停响应延迟）
    void restartFromCurrent();
    void wakeWorker();                  // 唤醒 worker（play/翻页/关闭时调用）
    void enqueuePiperJob(PiperJob &&job);

    DocManager *m_doc = nullptr;

    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_shutdownDone{false};
    Rate m_rate = Rate::Normal;
    std::atomic<int> m_wpm{175};   // worker 线程在合成前应用

    // worker 休眠/唤醒（替代 30ms 忙等轮询）
    std::mutex m_waitMutex;
    std::condition_variable m_waitCv;

    // ---- Piper 后端 ----
    bool m_piperAvailable = false;
    QString m_piperModel;     // .onnx 模型路径
    QString m_piperConfig;    // .json 配置路径
    QProcess *m_playProc = nullptr;     // 仅 worker 线程访问（aplay）
    QProcess *m_piperServer = nullptr;  // 仅 piper 线程访问（常驻合成服务）

    std::thread m_piperThread;          // 常驻合成服务专用线程
    std::mutex m_piperQueueMutex;       // 保护合成任务队列
    std::condition_variable m_piperCv;
    std::deque<PiperJob> m_piperJobs;
    bool m_piperStop = false;           // piper 线程终止信号（退出时清理 QProcess）

    void ensurePiperServer();                                      // 仅 piper 线程调用
    bool synthesizeOnPiperThread(const QString &text, const QString &wav); // 仅 piper 线程
    bool synthesizeToFile(const QString &text, const QString &wav);  // 任意线程：投递+等待
    void preSynthesize(const QString &text, const QString &wav);     // 任意线程：异步投递
    bool playWav(const QString &wav);    // 仅 worker 线程
    void reapPlaybackProc();             // 仅 worker 线程：回收 aplay 进程

    // 预合成缓存（piper 线程写 / worker 线程读，锁保护）
    std::mutex m_cacheMutex;
    QString m_cacheWav;
    bool m_haveCache = false;
};
