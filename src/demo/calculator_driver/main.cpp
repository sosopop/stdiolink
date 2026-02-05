/**
 * Calculator Driver - 计算器演示
 *
 * 功能演示:
 * 1. 多种数学运算命令
 * 2. 数值约束验证
 * 3. 事件流（计算进度）
 * 4. Builder API 构建元数据
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <cmath>

#include "stdiolink/driver/driver_core.h"
#include "stdiolink/driver/meta_builder.h"
#include "stdiolink/driver/meta_command_handler.h"

using namespace stdiolink;
using namespace stdiolink::meta;

class CalculatorHandler : public IMetaCommandHandler {
public:
    CalculatorHandler() { buildMeta(); }

    const DriverMeta& driverMeta() const override { return m_meta; }

    void handle(const QString& cmd, const QJsonValue& data,
                IResponder& resp) override
    {
        QJsonObject params = data.toObject();

        if (cmd == "add") {
            handleAdd(params, resp);
        } else if (cmd == "subtract") {
            handleSubtract(params, resp);
        } else if (cmd == "multiply") {
            handleMultiply(params, resp);
        } else if (cmd == "divide") {
            handleDivide(params, resp);
        } else if (cmd == "power") {
            handlePower(params, resp);
        } else if (cmd == "batch") {
            handleBatch(params, resp);
        } else if (cmd == "statistics") {
            handleStatistics(params, resp);
        } else {
            resp.error(404, QJsonObject{{"message", "unknown command: " + cmd}});
        }
    }

private:
    void buildMeta();
    void handleAdd(const QJsonObject& params, IResponder& resp);
    void handleSubtract(const QJsonObject& params, IResponder& resp);
    void handleMultiply(const QJsonObject& params, IResponder& resp);
    void handleDivide(const QJsonObject& params, IResponder& resp);
    void handlePower(const QJsonObject& params, IResponder& resp);
    void handleBatch(const QJsonObject& params, IResponder& resp);
    void handleStatistics(const QJsonObject& params, IResponder& resp);

    DriverMeta m_meta;
};

void CalculatorHandler::buildMeta()
{
    m_meta = DriverMetaBuilder()
        .schemaVersion("1.0")
        .info("demo.calculator", "Calculator Driver", "1.0.0",
              "数学计算器，演示数值约束和事件流")
        .vendor("stdiolink-demo")
        .command(CommandBuilder("add")
            .description("加法运算")
            .param(FieldBuilder("a", FieldType::Double)
                .required()
                .description("第一个操作数"))
            .param(FieldBuilder("b", FieldType::Double)
                .required()
                .description("第二个操作数"))
            .returns(FieldType::Double, "计算结果"))
        .command(CommandBuilder("subtract")
            .description("减法运算")
            .param(FieldBuilder("a", FieldType::Double).required())
            .param(FieldBuilder("b", FieldType::Double).required()))
        .command(CommandBuilder("multiply")
            .description("乘法运算")
            .param(FieldBuilder("a", FieldType::Double).required())
            .param(FieldBuilder("b", FieldType::Double).required()))
        .command(CommandBuilder("divide")
            .description("除法运算")
            .param(FieldBuilder("a", FieldType::Double).required())
            .param(FieldBuilder("b", FieldType::Double)
                .required()
                .description("除数（不能为0）")))
        .command(CommandBuilder("power")
            .description("幂运算")
            .param(FieldBuilder("base", FieldType::Double)
                .required()
                .description("底数"))
            .param(FieldBuilder("exponent", FieldType::Int)
                .required()
                .range(-10, 10)
                .description("指数 (-10 到 10)")))
        .command(CommandBuilder("batch")
            .description("批量计算，演示事件流")
            .param(FieldBuilder("operations", FieldType::Array)
                .required()
                .minItems(1)
                .maxItems(10)
                .items(FieldBuilder("op", FieldType::Object)
                    .addField(FieldBuilder("type", FieldType::Enum)
                        .required()
                        .enumValues(QStringList{"add", "sub", "mul", "div"}))
                    .addField(FieldBuilder("a", FieldType::Double).required())
                    .addField(FieldBuilder("b", FieldType::Double).required())))
            .event("progress", "计算进度"))
        .command(CommandBuilder("statistics")
            .description("统计计算")
            .param(FieldBuilder("numbers", FieldType::Array)
                .required()
                .minItems(1)
                .maxItems(100)
                .items(FieldBuilder("n", FieldType::Double))))
        .build();
}

void CalculatorHandler::handleAdd(const QJsonObject& params, IResponder& resp)
{
    double a = params["a"].toDouble();
    double b = params["b"].toDouble();
    resp.done(0, QJsonObject{{"result", a + b}});
}

void CalculatorHandler::handleSubtract(const QJsonObject& params, IResponder& resp)
{
    double a = params["a"].toDouble();
    double b = params["b"].toDouble();
    resp.done(0, QJsonObject{{"result", a - b}});
}

void CalculatorHandler::handleMultiply(const QJsonObject& params, IResponder& resp)
{
    double a = params["a"].toDouble();
    double b = params["b"].toDouble();
    resp.done(0, QJsonObject{{"result", a * b}});
}

void CalculatorHandler::handleDivide(const QJsonObject& params, IResponder& resp)
{
    double a = params["a"].toDouble();
    double b = params["b"].toDouble();
    if (b == 0) {
        resp.error(400, QJsonObject{{"message", "division by zero"}});
        return;
    }
    resp.done(0, QJsonObject{{"result", a / b}});
}

void CalculatorHandler::handlePower(const QJsonObject& params, IResponder& resp)
{
    double base = params["base"].toDouble();
    int exp = params["exponent"].toInt();
    resp.done(0, QJsonObject{{"result", std::pow(base, exp)}});
}

void CalculatorHandler::handleBatch(const QJsonObject& params, IResponder& resp)
{
    QJsonArray ops = params["operations"].toArray();
    QJsonArray results;
    int total = ops.size();

    for (int i = 0; i < total; ++i) {
        QJsonObject op = ops[i].toObject();
        QString type = op["type"].toString();
        double a = op["a"].toDouble();
        double b = op["b"].toDouble();
        double result = 0;

        if (type == "add") result = a + b;
        else if (type == "sub") result = a - b;
        else if (type == "mul") result = a * b;
        else if (type == "div") result = (b != 0) ? a / b : 0;

        results.append(result);
        resp.event("progress", 0, QJsonObject{
            {"current", i + 1}, {"total", total}, {"result", result}
        });
    }

    resp.done(0, QJsonObject{{"results", results}});
}

void CalculatorHandler::handleStatistics(const QJsonObject& params, IResponder& resp)
{
    QJsonArray nums = params["numbers"].toArray();
    double sum = 0, min = 0, max = 0;
    int count = nums.size();

    for (int i = 0; i < count; ++i) {
        double v = nums[i].toDouble();
        sum += v;
        if (i == 0) { min = max = v; }
        else { if (v < min) min = v; if (v > max) max = v; }
    }

    resp.done(0, QJsonObject{
        {"count", count}, {"sum", sum},
        {"avg", sum / count}, {"min", min}, {"max", max}
    });
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    CalculatorHandler handler;
    DriverCore core;
    core.setMetaHandler(&handler);
    return core.run(argc, argv);
}
