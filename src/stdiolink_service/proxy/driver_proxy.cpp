#include "driver_proxy.h"

#include <cstring>

JSValue createOpenDriverFunction(JSContext* ctx, JSValueConst driverCtor) {
    static const char kFactorySource[] =
        "(function(DriverCtor){\n"
        "  return async function openDriver(program, args = []) {\n"
        "    const startArgs = Array.isArray(args) ? args.slice() : [];\n"
        "    const hasProfileArg = startArgs.some(a => typeof a === 'string' && a.startsWith('--profile='));\n"
        "    if (!hasProfileArg) {\n"
        "      startArgs.push('--profile=keepalive');\n"
        "    }\n"
        "    const driver = new DriverCtor();\n"
        "    if (!driver.start(program, startArgs)) {\n"
        "      throw new Error('Failed to start driver: ' + program);\n"
        "    }\n"
        "    const meta = driver.queryMeta(5000);\n"
        "    if (!meta) {\n"
        "      driver.terminate();\n"
        "      throw new Error('Failed to query metadata from: ' + program);\n"
        "    }\n"
        "    const commands = new Set((meta.commands || []).map(c => c.name));\n"
        "    let busy = false;\n"
        "    return new Proxy(driver, {\n"
        "      get(target, prop) {\n"
        "        if (prop === '$driver') return target;\n"
        "        if (prop === '$meta') return meta;\n"
        "        if (prop === '$rawRequest') return (cmd, data) => target.request(cmd, data || {});\n"
        "        if (prop === '$close') return () => target.terminate();\n"
        "        if (typeof prop === 'string' && commands.has(prop)) {\n"
        "          return (params = {}) => {\n"
        "            if (busy) {\n"
        "              throw new Error('DriverBusyError: request already in flight');\n"
        "            }\n"
        "            busy = true;\n"
        "            let task;\n"
        "            try {\n"
        "              task = target.request(prop, params);\n"
        "            } catch (e) {\n"
        "              busy = false;\n"
        "              throw e;\n"
        "            }\n"
        "            return globalThis.__scheduleTask(task).then(\n"
        "              (msg) => {\n"
        "                busy = false;\n"
        "                if (!msg) {\n"
        "                  throw new Error('No response for command: ' + prop);\n"
        "                }\n"
        "                if (msg.status === 'error') {\n"
        "                  const data = (msg.data && typeof msg.data === 'object') ? msg.data : {};\n"
        "                  const err = new Error(data.message || ('Command failed: ' + prop));\n"
        "                  err.code = msg.code;\n"
        "                  err.data = msg.data;\n"
        "                  throw err;\n"
        "                }\n"
        "                return msg.data;\n"
        "              },\n"
        "              (err) => {\n"
        "                busy = false;\n"
        "                throw err;\n"
        "              }\n"
        "            );\n"
        "          };\n"
        "        }\n"
        "        return undefined;\n"
        "      }\n"
        "    });\n"
        "  };\n"
        "})";

    JSValue factory = JS_Eval(ctx, kFactorySource, std::strlen(kFactorySource),
                              "<stdiolink/open_driver_factory>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(factory)) {
        return factory;
    }

    JSValue args[1] = {driverCtor};
    JSValue openDriver = JS_Call(ctx, factory, JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, factory);
    return openDriver;
}
