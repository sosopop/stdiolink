import { add, scaledAdd } from "./base.js";
import { MODULE_LABEL } from "../shared/constants.js";

export function buildReport(a, b) {
    return {
        label: MODULE_LABEL,
        sum: add(a, b),
        scaledSum: scaledAdd(a, b)
    };
}
