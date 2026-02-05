#include "modbus_types.h"
#include <cstring>

namespace modbus {

int registersPerType(DataType type)
{
    switch (type) {
    case DataType::Int16:
    case DataType::UInt16:
        return 1;
    case DataType::Int32:
    case DataType::UInt32:
    case DataType::Float32:
        return 2;
    case DataType::Int64:
    case DataType::UInt64:
    case DataType::Float64:
        return 4;
    }
    return 1;
}

ByteOrder parseByteOrder(const QString& str)
{
    if (str == "little_endian") return ByteOrder::LittleEndian;
    if (str == "big_endian_byte_swap") return ByteOrder::BigEndianByteSwap;
    if (str == "little_endian_byte_swap") return ByteOrder::LittleEndianByteSwap;
    return ByteOrder::BigEndian; // 默认
}

DataType parseDataType(const QString& str)
{
    if (str == "int16") return DataType::Int16;
    if (str == "uint16") return DataType::UInt16;
    if (str == "int32") return DataType::Int32;
    if (str == "uint32") return DataType::UInt32;
    if (str == "float32") return DataType::Float32;
    if (str == "int64") return DataType::Int64;
    if (str == "uint64") return DataType::UInt64;
    if (str == "float64") return DataType::Float64;
    return DataType::UInt16; // 默认
}

QString exceptionMessage(ExceptionCode code)
{
    switch (code) {
    case ExceptionCode::None:
        return "No error";
    case ExceptionCode::IllegalFunction:
        return "Illegal function";
    case ExceptionCode::IllegalDataAddress:
        return "Illegal data address";
    case ExceptionCode::IllegalDataValue:
        return "Illegal data value";
    case ExceptionCode::SlaveDeviceFailure:
        return "Slave device failure";
    case ExceptionCode::Acknowledge:
        return "Acknowledge";
    case ExceptionCode::SlaveDeviceBusy:
        return "Slave device busy";
    case ExceptionCode::MemoryParityError:
        return "Memory parity error";
    case ExceptionCode::GatewayPathUnavailable:
        return "Gateway path unavailable";
    case ExceptionCode::GatewayTargetDeviceFailedToRespond:
        return "Gateway target device failed to respond";
    }
    return QString("Unknown exception: 0x%1").arg(static_cast<int>(code), 2, 16, QChar('0'));
}

// ByteOrderConverter 实现

ByteOrderConverter::ByteOrderConverter(ByteOrder order)
    : m_order(order)
{
}

uint16_t ByteOrderConverter::toUInt16(const QVector<uint16_t>& regs, int offset) const
{
    if (offset >= regs.size()) return 0;
    return regs[offset];
}

int16_t ByteOrderConverter::toInt16(const QVector<uint16_t>& regs, int offset) const
{
    return static_cast<int16_t>(toUInt16(regs, offset));
}

uint32_t ByteOrderConverter::combineRegisters32(uint16_t high, uint16_t low) const
{
    switch (m_order) {
    case ByteOrder::BigEndian:
        return (static_cast<uint32_t>(high) << 16) | low;
    case ByteOrder::LittleEndian:
        return (static_cast<uint32_t>(low) << 16) | high;
    case ByteOrder::BigEndianByteSwap: {
        uint16_t h = ((high & 0xFF) << 8) | ((high >> 8) & 0xFF);
        uint16_t l = ((low & 0xFF) << 8) | ((low >> 8) & 0xFF);
        return (static_cast<uint32_t>(h) << 16) | l;
    }
    case ByteOrder::LittleEndianByteSwap: {
        uint16_t h = ((high & 0xFF) << 8) | ((high >> 8) & 0xFF);
        uint16_t l = ((low & 0xFF) << 8) | ((low >> 8) & 0xFF);
        return (static_cast<uint32_t>(l) << 16) | h;
    }
    }
    return 0;
}

uint32_t ByteOrderConverter::toUInt32(const QVector<uint16_t>& regs, int offset) const
{
    if (offset + 1 >= regs.size()) return 0;
    return combineRegisters32(regs[offset], regs[offset + 1]);
}

int32_t ByteOrderConverter::toInt32(const QVector<uint16_t>& regs, int offset) const
{
    return static_cast<int32_t>(toUInt32(regs, offset));
}

float ByteOrderConverter::toFloat32(const QVector<uint16_t>& regs, int offset) const
{
    uint32_t raw = toUInt32(regs, offset);
    float result;
    std::memcpy(&result, &raw, sizeof(float));
    return result;
}

uint64_t ByteOrderConverter::combineRegisters64(uint16_t r0, uint16_t r1, uint16_t r2, uint16_t r3) const
{
    uint32_t high = combineRegisters32(r0, r1);
    uint32_t low = combineRegisters32(r2, r3);

    switch (m_order) {
    case ByteOrder::BigEndian:
    case ByteOrder::BigEndianByteSwap:
        return (static_cast<uint64_t>(high) << 32) | low;
    case ByteOrder::LittleEndian:
    case ByteOrder::LittleEndianByteSwap:
        return (static_cast<uint64_t>(low) << 32) | high;
    }
    return 0;
}

uint64_t ByteOrderConverter::toUInt64(const QVector<uint16_t>& regs, int offset) const
{
    if (offset + 3 >= regs.size()) return 0;
    return combineRegisters64(regs[offset], regs[offset+1], regs[offset+2], regs[offset+3]);
}

int64_t ByteOrderConverter::toInt64(const QVector<uint16_t>& regs, int offset) const
{
    return static_cast<int64_t>(toUInt64(regs, offset));
}

double ByteOrderConverter::toFloat64(const QVector<uint16_t>& regs, int offset) const
{
    uint64_t raw = toUInt64(regs, offset);
    double result;
    std::memcpy(&result, &raw, sizeof(double));
    return result;
}

void ByteOrderConverter::splitRegisters32(uint32_t value, uint16_t& high, uint16_t& low) const
{
    switch (m_order) {
    case ByteOrder::BigEndian:
        high = static_cast<uint16_t>(value >> 16);
        low = static_cast<uint16_t>(value & 0xFFFF);
        break;
    case ByteOrder::LittleEndian:
        low = static_cast<uint16_t>(value >> 16);
        high = static_cast<uint16_t>(value & 0xFFFF);
        break;
    case ByteOrder::BigEndianByteSwap: {
        uint16_t h = static_cast<uint16_t>(value >> 16);
        uint16_t l = static_cast<uint16_t>(value & 0xFFFF);
        high = ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
        low = ((l & 0xFF) << 8) | ((l >> 8) & 0xFF);
        break;
    }
    case ByteOrder::LittleEndianByteSwap: {
        uint16_t h = static_cast<uint16_t>(value >> 16);
        uint16_t l = static_cast<uint16_t>(value & 0xFFFF);
        low = ((h & 0xFF) << 8) | ((h >> 8) & 0xFF);
        high = ((l & 0xFF) << 8) | ((l >> 8) & 0xFF);
        break;
    }
    }
}

void ByteOrderConverter::splitRegisters64(uint64_t value, uint16_t& r0, uint16_t& r1, uint16_t& r2, uint16_t& r3) const
{
    uint32_t high, low;
    switch (m_order) {
    case ByteOrder::BigEndian:
    case ByteOrder::BigEndianByteSwap:
        high = static_cast<uint32_t>(value >> 32);
        low = static_cast<uint32_t>(value & 0xFFFFFFFF);
        break;
    default:
        low = static_cast<uint32_t>(value >> 32);
        high = static_cast<uint32_t>(value & 0xFFFFFFFF);
        break;
    }
    splitRegisters32(high, r0, r1);
    splitRegisters32(low, r2, r3);
}

QVector<uint16_t> ByteOrderConverter::fromUInt16(uint16_t value) const
{
    return {value};
}

QVector<uint16_t> ByteOrderConverter::fromInt16(int16_t value) const
{
    return fromUInt16(static_cast<uint16_t>(value));
}

QVector<uint16_t> ByteOrderConverter::fromUInt32(uint32_t value) const
{
    uint16_t high, low;
    splitRegisters32(value, high, low);
    return {high, low};
}

QVector<uint16_t> ByteOrderConverter::fromInt32(int32_t value) const
{
    return fromUInt32(static_cast<uint32_t>(value));
}

QVector<uint16_t> ByteOrderConverter::fromFloat32(float value) const
{
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    return fromUInt32(raw);
}

QVector<uint16_t> ByteOrderConverter::fromUInt64(uint64_t value) const
{
    uint16_t r0, r1, r2, r3;
    splitRegisters64(value, r0, r1, r2, r3);
    return {r0, r1, r2, r3};
}

QVector<uint16_t> ByteOrderConverter::fromInt64(int64_t value) const
{
    return fromUInt64(static_cast<uint64_t>(value));
}

QVector<uint16_t> ByteOrderConverter::fromFloat64(double value) const
{
    uint64_t raw;
    std::memcpy(&raw, &value, sizeof(double));
    return fromUInt64(raw);
}

} // namespace modbus
