#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"
#include <QCoreApplication>
#include <QDebug>

using namespace stdiolink;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString driverPath = app.applicationDirPath();

    // 示例 1: 单个 Driver 调用
    qDebug() << "=== Echo Driver Demo ===";
    {
        Driver d;
        if (!d.start(driverPath + "/echo_driver.exe")) {
            qDebug() << "Failed to start echo_driver";
            return 1;
        }

        Task t = d.request("echo", QJsonObject{{"msg", "Hello, stdiolink!"}});

        Message msg;
        if (t.waitNext(msg, 5000)) {
            qDebug() << "Response:" << msg.status << msg.payload;
        }

        d.terminate();
    }

    // 示例 2: 多 Driver 并发
    qDebug() << "\n=== Multi-Driver Demo ===";
    {
        Driver d1, d2;
        d1.start(driverPath + "/echo_driver.exe");
        d2.start(driverPath + "/progress_driver.exe");

        QVector<Task> tasks;
        tasks << d1.request("echo", QJsonObject{{"msg", "task1"}});
        tasks << d2.request("progress", QJsonObject{{"steps", 3}});

        AnyItem item;
        while (waitAnyNext(tasks, item, 5000)) {
            qDebug() << "Task" << item.taskIndex
                     << ":" << item.msg.status
                     << item.msg.payload;
        }

        d1.terminate();
        d2.terminate();
    }

    qDebug() << "\nDemo completed.";
    return 0;
}
