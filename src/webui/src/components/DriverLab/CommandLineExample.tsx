import React from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, Button, message } from 'antd';
import { CopyOutlined } from '@ant-design/icons';

interface CommandLineExampleProps {
  driverId: string | null;
  command: string | null;
  params: Record<string, unknown>;
}

function formatValue(value: unknown): string {
  if (value === null || value === undefined || value === '') return '';
  if (typeof value === 'boolean') return value ? 'true' : 'false';
  if (typeof value === 'number') return Number.isInteger(value) ? String(value) : String(value);
  if (typeof value === 'string') {
    if (/[\s"'\\|&;<>()$`!]/.test(value)) return `"${value}"`;
    return value;
  }
  // Object or Array
  return `"${JSON.stringify(value)}"`;
}

export function buildCommandLine(
  driverId: string | null,
  command: string | null,
  params: Record<string, unknown>,
): string {
  if (!driverId || !command) return '';
  const parts = [driverId, `--cmd=${command}`];
  for (const [key, value] of Object.entries(params)) {
    const formatted = formatValue(value);
    if (!formatted) continue;
    parts.push(`--${key}=${formatted}`);
  }
  return parts.join(' ');
}

export function buildArgsLine(
  command: string | null,
  params: Record<string, unknown>,
): string {
  if (!command) return '';
  const parts = [`--cmd=${command}`];
  for (const [key, value] of Object.entries(params)) {
    const formatted = formatValue(value);
    if (!formatted) continue;
    parts.push(`--${key}=${formatted}`);
  }
  return parts.join(' ');
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
