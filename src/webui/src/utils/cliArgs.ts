function isPlainObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function encodePathKey(key: string, isRoot: boolean): string {
  if (/^[A-Za-z_][A-Za-z0-9_-]*$/.test(key)) {
    return isRoot ? key : `.${key}`;
  }
  const escaped = key.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
  return `["${escaped}"]`;
}

function canonicalLiteral(value: unknown): string {
  return JSON.stringify(value);
}

function renderPath(prefix: string, value: unknown, out: string[], optionPrefix: string): void {
  if (value === undefined) {
    return;
  }

  if (Array.isArray(value)) {
    if (value.length === 0) {
      out.push(`${optionPrefix}${prefix}=[]`);
      return;
    }
    value.forEach((item, index) => {
      renderPath(`${prefix}[${index}]`, item, out, optionPrefix);
    });
    return;
  }

  if (isPlainObject(value)) {
    const entries = Object.entries(value).sort(([a], [b]) => a.localeCompare(b));
    if (entries.length === 0) {
      out.push(`${optionPrefix}${prefix}={}`);
      return;
    }
    entries.forEach(([key, child]) => {
      renderPath(`${prefix}${encodePathKey(key, false)}`, child, out, optionPrefix);
    });
    return;
  }

  out.push(`${optionPrefix}${prefix}=${canonicalLiteral(value)}`);
}

export function renderCliArgs(params: Record<string, unknown>, optionPrefix = '--'): string[] {
  const out: string[] = [];
  Object.keys(params)
    .sort((a, b) => a.localeCompare(b))
    .forEach((key) => {
      renderPath(encodePathKey(key, true), params[key], out, optionPrefix);
    });
  return out;
}
