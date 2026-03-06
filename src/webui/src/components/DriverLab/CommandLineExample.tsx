import React from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, Button, message } from 'antd';
import { CopyOutlined } from '@ant-design/icons';

interface CommandLineExampleProps {
  driverId: string | null;
  command: string | null;
  params: Record<string, unknown>;
}

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

function renderPath(prefix: string, value: unknown, out: string[]): void {
  if (value === undefined) {
    return;
  }

  if (Array.isArray(value)) {
    if (value.length === 0) {
      out.push(`--${prefix}=[]`);
      return;
    }
    value.forEach((item, index) => {
      renderPath(`${prefix}[${index}]`, item, out);
    });
    return;
  }

  if (isPlainObject(value)) {
    const entries = Object.entries(value).sort(([a], [b]) => a.localeCompare(b));
    if (entries.length === 0) {
      out.push(`--${prefix}={}`);
      return;
    }
    entries.forEach(([key, child]) => {
      renderPath(`${prefix}${encodePathKey(key, false)}`, child, out);
    });
    return;
  }

  out.push(`--${prefix}=${canonicalLiteral(value)}`);
}

export function renderCliArgs(params: Record<string, unknown>): string[] {
  const out: string[] = [];
  Object.keys(params)
    .sort((a, b) => a.localeCompare(b))
    .forEach((key) => {
      renderPath(encodePathKey(key, true), params[key], out);
    });
  return out;
}

export function buildCommandLine(
  driverId: string | null,
  command: string | null,
  params: Record<string, unknown>,
): string {
  if (!driverId || !command) return '';
  return [driverId, `--cmd=${command}`, ...renderCliArgs(params)].join(' ');
}

export function buildArgsLine(
  command: string | null,
  params: Record<string, unknown>,
): string {
  if (!command) return '';
  return [`--cmd=${command}`, ...renderCliArgs(params)].join(' ');
}

export const CommandLineExample: React.FC<CommandLineExampleProps> = ({
  command,
  params,
}) => {
  const { t } = useTranslation();
  const argsLine = buildArgsLine(command, params);

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(argsLine);
      message.success(t('driverlab.cmdline.copied'));
    } catch {
      message.error(t('driverlab.cmdline.copy_failed'));
    }
  };

  if (!command) {
    return (
      <div data-testid="cmdline-example">
        <Typography.Text type="secondary" data-testid="cmdline-placeholder">
          {t('driverlab.cmdline.placeholder')}
        </Typography.Text>
      </div>
    );
  }

  return (
    <div data-testid="cmdline-example" style={{ marginTop: 16 }}>
      <Typography.Text
        type="secondary"
        data-testid="cmdline-label"
        style={{ display: 'block', marginBottom: 4, fontSize: 12 }}
      >
        {t('driverlab.cmdline.label')}
      </Typography.Text>
      <div style={{ position: 'relative' }}>
        <pre
          data-testid="cmdline-text"
          style={{
            background: 'var(--surface-layer2, #1a1a2e)',
            padding: '12px 40px 12px 12px',
            borderRadius: 6,
            fontSize: 12,
            fontFamily: 'monospace',
            whiteSpace: 'pre-wrap',
            wordBreak: 'break-all',
            margin: 0,
          }}
        >
          {argsLine}
        </pre>
        <Button
          type="text"
          size="small"
          icon={<CopyOutlined />}
          onClick={handleCopy}
          data-testid="cmdline-copy"
          style={{ position: 'absolute', top: '50%', right: 4, transform: 'translateY(-50%)' }}
        />
      </div>
    </div>
  );
};
