// demo script: requires stdio.drv.device-simulator in data_root/drivers/
import { getConfig, openDriver } from "stdiolink";
import { createLogger } from "stdiolink/log";
import { resolveDriver } from "stdiolink/driver";

const config = getConfig();
const { service_name, log_level, devices } = config;
const logger = createLogger({ service: service_name });

logger.info("multi_device_service starting", {
  logLevel: log_level,
  totalDevices: devices.length,
});

const enabledDevices = devices.filter((d) => d.enabled !== false);
logger.info(`enabling ${enabledDevices.length} / ${devices.length} devices`);

(async () => {
  for (const device of enabledDevices) {
    logger.info("connecting device", {
      id: device.id,
      host: device.host,
      port: device.port,
      driver: device.driver,
    });

    const driverPath = resolveDriver("stdio.drv.device-simulator");

    try {
      const driver = await openDriver(driverPath);
      const result = await driver.scan({ mode: "quick" });
      logger.info(`device ${device.id} responded`, { result });
      driver.$close();
    } catch (err) {
      logger.warn(`device ${device.id} connection failed`, { error: String(err) });
    }
  }

  logger.info("multi_device_service completed");
})();
