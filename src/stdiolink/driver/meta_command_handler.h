#pragma once

#include "icommand_handler.h"
#include "stdiolink/protocol/meta_types.h"

namespace stdiolink {

/**
 * 支持元数据的命令处理器接口
 * 继承自 ICommandHandler，添加元数据支持
 */
class IMetaCommandHandler : public ICommandHandler {
public:
    /**
     * 返回 Driver 的元数据描述
     * 子类必须实现此方法
     */
    virtual const meta::DriverMeta& driverMeta() const = 0;

    /**
     * 是否自动验证参数
     * 默认为 true，框架会在调用 handle() 前自动验证
     */
    virtual bool autoValidateParams() const { return true; }
};

} // namespace stdiolink
