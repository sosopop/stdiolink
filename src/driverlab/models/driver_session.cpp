#include "driver_session.h"
#include <QPointer>

DriverSession::DriverSession(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(50);
    connect(m_pollTimer, &QTimer::timeout, this, &DriverSession::pollMessages);
}

DriverSession::~DriverSession()
{
    stop();
}

bool DriverSession::start(const QString &program, const QStringList &args)
{
    if (m_driver && m_driver->isRunning()) {
        stop();
    }

    m_program = program;
    m_driver = std::make_unique<stdiolink::Driver>();

    if (!m_driver->start(program, args)) {
        emit errorOccurred(tr("启动 Driver 失败: %1").arg(program));
        m_driver.reset();
        return false;
    }

    emit started();
    queryMetaAsync();
    return true;
}

void DriverSession::stop()
{
    m_pollTimer->stop();
    m_currentTask = stdiolink::Task();

    if (m_driver) {
        m_driver->terminate();
        m_driver.reset();
    }

    emit stopped();
}

bool DriverSession::isRunning() const
{
    return m_driver && m_driver->isRunning();
}

const stdiolink::meta::DriverMeta *DriverSession::meta() const
{
    // 优先返回缓存的元数据
    if (m_cachedMeta) {
        return m_cachedMeta.get();
    }
    if (!m_driver) return nullptr;
    return m_driver->hasMeta() ? m_driver->queryMeta(0) : nullptr;
}

bool DriverSession::hasMeta() const
{
    return m_cachedMeta || (m_driver && m_driver->hasMeta());
}

void DriverSession::setRunMode(RunMode mode)
{
    if (m_runMode == mode) return;

    // 从 KeepAlive 切换到 OneShot 时，关闭正在运行的进程
    if (m_runMode == KeepAlive && mode == OneShot) {
        if (m_driver && m_driver->isRunning()) {
            m_driver->terminate();
        }
    }

    m_runMode = mode;
}

void DriverSession::executeCommand(const QString &cmd, const QJsonObject &data)
{
    // 如果 Driver 没有运行，重新启动
    if (!m_driver || !m_driver->isRunning()) {
        m_driver = std::make_unique<stdiolink::Driver>();
        QStringList args;
        if (m_runMode == KeepAlive) {
            args << "--profile=keepalive";
        }
        if (!m_driver->start(m_program, args)) {
            emit errorOccurred(tr("启动 Driver 失败: %1").arg(m_program));
            m_driver.reset();
            return;
        }
    }

    m_currentTask = m_driver->request(cmd, data);
    m_pollTimer->start();
}

void DriverSession::cancelCurrentTask()
{
    m_pollTimer->stop();
    m_currentTask = stdiolink::Task();
}

void DriverSession::pollMessages()
{
    if (!m_currentTask.isValid()) {
        m_pollTimer->stop();
        return;
    }

    m_driver->pumpStdout();

    stdiolink::Message msg;
    while (m_currentTask.tryNext(msg)) {
        emit messageReceived(msg);

        if (m_currentTask.isDone()) {
            m_pollTimer->stop();
            emit taskCompleted(
                m_currentTask.exitCode(),
                m_currentTask.finalPayload(),
                m_currentTask.errorText()
            );
            m_currentTask = stdiolink::Task();
            return;
        }
    }
}

void DriverSession::queryMetaAsync()
{
    if (!m_driver || m_queryingMeta) return;

    m_queryingMeta = true;

    // 使用 QPointer 防止悬空指针
    QPointer<DriverSession> self = this;

    // 延迟执行同步查询，避免阻塞启动
    QTimer::singleShot(0, this, [self]() {
        // 检查对象是否仍然有效
        if (!self) return;

        if (!self->m_driver) {
            self->m_queryingMeta = false;
            return;
        }

        // 使用同步方法查询元数据（内部会处理消息泵送）
        const auto *meta = self->m_driver->queryMeta(5000);
        self->m_queryingMeta = false;

        if (meta) {
            // 缓存元数据的副本
            self->m_cachedMeta = std::make_unique<stdiolink::meta::DriverMeta>(*meta);
            emit self->metaReady(self->m_cachedMeta.get());
        } else {
            emit self->errorOccurred(tr("获取 Driver 元数据失败"));
        }
    });
}
