// stdiolink 单头文件入口
// 此文件由 pack_single_header.py 自动生成

#pragma once

// Protocol
#include "stdiolink/protocol/jsonl_types.h"
#include "stdiolink/protocol/jsonl_serializer.h"
#include "stdiolink/protocol/jsonl_parser.h"

// Driver
#include "stdiolink/driver/iresponder.h"
#include "stdiolink/driver/icommand_handler.h"
#include "stdiolink/driver/stdio_responder.h"
#include "stdiolink/driver/driver_core.h"

// Host
#include "stdiolink/host/task_state.h"
#include "stdiolink/host/task.h"
#include "stdiolink/host/driver.h"
#include "stdiolink/host/wait_any.h"

// Console
#include "stdiolink/console/console_args.h"
#include "stdiolink/console/console_responder.h"
