import { SCALE_FACTOR } from "../shared/constants.js";

export function add(a, b) {
    return a + b;
}

export function scaledAdd(a, b) {
    return add(a, b) * SCALE_FACTOR;
}
