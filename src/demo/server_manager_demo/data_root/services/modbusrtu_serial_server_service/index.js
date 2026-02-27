/**
 * modbusrtu_serial_server_service — Modbus RTU 串口从站服务
 *
 * 启动 Modbus RTU Serial Server 驱动，打开串口并注册从站 Unit。
 * keepalive 模式：驱动进程持续运行，不调用 $close()。
 */
import { openDriver } from "stdiolink";
import { getConfig } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { sleep } from "stdiolink/time";
import { findDriverPath } from "../../shared/lib/driver_utils.js";

const cfg = getConfig();
const portName = String(cfg.port_name ?? "COM1");
const baudRate = Number(cfg.baud_rate ?? 9600);
const dataBits = Number(cfg.data_bits ?? 8);
const stopBits = String(cfg.stop_bits ?? "1");
const parity = String(cfg.parity ?? "none");
const unitIds = String(cfg.unit_ids ?? "1");
const dataAreaSize = Number(cfg.data_area_size ?? 10000);
const eventMode = String(cfg.event_mode ?? "write");

const logger = createLogger({ service: "modbusrtu_serial_server" });

function parseUnitIds(str) {
    return str.split(",")
        .map(s => Number(s.trim()))
        .filter(n => Number.isInteger(n) && n > 0 && n <= 247);
}

(async () => {
    logger.info("starting", { portName, baudRate, dataBits, stopBits, parity,
                               unitIds, dataAreaSize, eventMode });

    const driverPath = findDriverPath("stdio.drv.modbusrtu_serial_server");
    if (!driverPath) throw new Error("stdio.drv.modbusrtu_serial_server not found");

    const drv = await openDriver(driverPath);
    logger.info("driver opened");

    const startResult = await drv.start_server({
        port_name: portName,
        baud_rate: baudRate,
        data_bits: dataBits,
        stop_bits: stopBits,
        parity: parity,
        event_mode: eventMode
    });
    logger.info("server started", startResult);

    const ids = parseUnitIds(unitIds);
    if (ids.length === 0) {
        logger.warn("no valid unit_ids configured");
    }
    for (const id of ids) {
        const r = await drv.add_unit({ unit_id: id, data_area_size: dataAreaSize });
        logger.info(`unit ${id} added`, r);
    }

    const status = await drv.status();
    logger.info("server ready", status);

    // keepalive: hold event loop alive while driver subprocess runs
    while (true) {
        await sleep(60000);
    }
})();
