#pragma once

#include "stdiolink/protocol/meta_types.h"
#include "stdiolink/stdiolink_export.h"

namespace stdiolink::meta {

// 为命令元数据补齐示例。若命令没有示例，将自动补一条 stdio 示例；
// 当 addConsoleExamples=true 时，同时尽量补一条 console 示例。
STDIOLINK_API void ensureCommandExamples(DriverMeta& meta, bool addConsoleExamples = true);

} // namespace stdiolink::meta

