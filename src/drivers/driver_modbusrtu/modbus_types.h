#ifndef MODBUS_TYPES_H
#define MODBUS_TYPES_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>

namespace modbus {

/**
 * Modbus 功能码
 */
enum class FunctionCode : uint8_t {
    ReadCoils = 0x01,
    ReadDiscreteInputs = 0x02,
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleCoil = 0x05,
    WriteSingleRegister = 0x06,
    WriteMultipleCoils = 0x0F,
    WriteMultipleRegisters = 0x10
};

/**
 * Modbus 异常码
 */
enum class ExceptionCode : uint8_t {
    None = 0x00,
    IllegalFunction = 0x01,
    IllegalDataAddress = 0x02,
    IllegalDataValue = 0x03,
    SlaveDeviceFailure = 0x04,
    Acknowledge = 0x05,
    SlaveDeviceBusy = 0x06,
    MemoryParityError = 0x08,
    GatewayPathUnavailable = 0x0A,
    GatewayTargetDeviceFailedToRespond = 0x0B
};

/**
 * 字节序类型
 */
enum class ByteOrder {
    BigEndian,           // AB CD (Modbus 标准)
    LittleEndian,        // CD AB
    BigEndianByteSwap,   // BA DC
    LittleEndianByteSwap // DC BA
};

/**
 * 数据类型
 */
enum class DataType {
    Int16,
    UInt16,
    Int32,
    UInt32,
    Float32,
    Int64,
    UInt64,
    Float64
};

/**
 * 获取数据类型所需的寄存器数量
 */
int registersPerType(DataType type);

/**
 * 从字符串解析字节序
 */
ByteOrder parseByteOrder(const QString& str);

/**
 * 从字符串解析数据类型
 */
DataType parseDataType(const QString& str);

/**
 * 获取异常码描述
 */
QString exceptionMessage(ExceptionCode code);

/**
 * 字节序转换器
 */
class ByteOrderConverter {
public:
    explicit ByteOrderConverter(ByteOrder order = ByteOrder::BigEndian);

    // 寄存器数组 -> 数值
    int16_t toInt16(const QVector<uint16_t>& regs, int offset = 0) const;
    uint16_t toUInt16(const QVector<uint16_t>& regs, int offset = 0) const;
    int32_t toInt32(const QVector<uint16_t>& regs, int offset = 0) const;
    uint32_t toUInt32(const QVector<uint16_t>& regs, int offset = 0) const;
    float toFloat32(const QVector<uint16_t>& regs, int offset = 0) const;
    int64_t toInt64(const QVector<uint16_t>& regs, int offset = 0) const;
    uint64_t toUInt64(const QVector<uint16_t>& regs, int offset = 0) const;
    double toFloat64(const QVector<uint16_t>& regs, int offset = 0) const;

    // 数值 -> 寄存器数组
    QVector<uint16_t> fromInt16(int16_t value) const;
    QVector<uint16_t> fromUInt16(uint16_t value) const;
    QVector<uint16_t> fromInt32(int32_t value) const;
    QVector<uint16_t> fromUInt32(uint32_t value) const;
    QVector<uint16_t> fromFloat32(float value) const;
    QVector<uint16_t> fromInt64(int64_t value) const;
    QVector<uint16_t> fromUInt64(uint64_t value) const;
    QVector<uint16_t> fromFloat64(double value) const;

private:
    ByteOrder m_order;

    // 内部辅助函数
    uint32_t combineRegisters32(uint16_t high, uint16_t low) const;
    uint64_t combineRegisters64(uint16_t r0, uint16_t r1, uint16_t r2, uint16_t r3) const;
    void splitRegisters32(uint32_t value, uint16_t& high, uint16_t& low) const;
    void splitRegisters64(uint64_t value, uint16_t& r0, uint16_t& r1, uint16_t& r2, uint16_t& r3) const;
};

} // namespace modbus

#endif // MODBUS_TYPES_H
