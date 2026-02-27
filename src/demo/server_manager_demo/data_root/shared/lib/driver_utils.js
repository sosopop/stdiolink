/**
 * driver_utils.js — 共享 Driver 路径探测工具
 *
 * 在 release 包布局和开发构建目录下探测 driver 可执行文件路径。
 */

import { SYSTEM } from "stdiolink/constants";
import { join } from "stdiolink/path";
import { exists } from "stdiolink/fs";

/**
 * 在常见位置探测 driver 可执行文件路径。
 * @param {string} baseName - driver 基础名称（不含扩展名）
 * @returns {string|null} 找到的路径，或 null
 */
export function findDriverPath(baseName) {
    const ext = SYSTEM.isWindows ? ".exe" : "";
    const name = baseName + ext;

    const candidates = [
        `./${name}`,
        `./bin/${name}`,
        `../bin/${name}`,
        `../../bin/${name}`,
        `../../../bin/${name}`,
        `./build/bin/${name}`,
        `../../../../../build/bin/${name}`,
    ];

    for (const p of candidates) {
        if (exists(p)) {
            return p;
        }
    }
    return null;
}

/**
 * 探测并返回 driver 路径，找不到则抛出异常。
 * @param {string} baseName
 * @returns {string}
 */
export function requireDriverPath(baseName) {
    const p = findDriverPath(baseName);
    if (!p) {
        throw new Error(`driver not found: ${baseName}`);
    }
    return p;
}
