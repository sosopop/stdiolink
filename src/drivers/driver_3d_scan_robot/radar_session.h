#pragma once

#include <QByteArray>
#include <QString>

#include "driver_3d_scan_robot/protocol_codec.h"
#include "driver_3d_scan_robot/radar_transport.h"

namespace scan_robot {

struct QueryTaskResult {
    quint8  lastCounter = 0;
    quint8  lastCommand = 0;
    quint32 resultCode  = 0;
};

struct ScanAggregateResult {
    quint8     taskCounter  = 0;
    quint8     taskCommand  = 0;
    quint32    resultCode   = 0;
    int        segmentCount = 0;
    int        byteCount    = 0;
    QByteArray data;
};

enum class SessionErrorKind {
    None,
    Transport,
    Protocol,
};

// RadarSession 封装：打开连接 → 发命令 → 等响应/轮询 → 拉数据 → 关闭
// oneshot 模式：构造时传入 transport，每次 open → 操作 → close
class RadarSession {
public:
    explicit RadarSession(IRadarTransport* transport);
    ~RadarSession();

    bool open(const RadarTransportParams& params, QString* errorMessage);
    void close();

    // ── 简单命令（发一帧，收一帧）──────────────────────
    bool sendAndReceive(quint8 command, const QByteArray& payload,
                        RadarFrame* response, QString* errorMessage,
                        bool interrupt = false,
                        SessionErrorKind* errorKind = nullptr);

    // ── 长任务轮询 ──────────────────────────────────────
    bool waitTaskCompleted(quint8 expectedCounter, quint8 expectedCommand,
                           int taskTimeoutMs, int queryIntervalMs, int interCmdDelayMs,
                           QueryTaskResult* result, QString* errorMessage);

    // ── 分段数据拉取 ────────────────────────────────────
    bool collectScanData(int totalBytes, int interCmdDelayMs,
                         ScanAggregateResult* result, QString* errorMessage);

    quint8 nextCounter();
    quint8 nextInsertCounter();
    quint8 addr() const { return m_params.addr; }

private:
    IRadarTransport*    m_transport;
    RadarTransportParams m_params;
    quint8              m_counter       = 0;
    quint8              m_insertCounter = 0;

    bool readSegmentSize(quint16* segSize, QString* errorMessage);
};

} // namespace scan_robot
