import React from 'react';
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

export const CommandLineExample: React.FC<CommandLineExampleProps> = ({
  driverId,
  command,
  params,
}) => {
  const cmdLine = buildCommandLine(driverId, command, params);

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(cmdLine);
      message.success('Copied');
    } catch {
      message.error('Copy failed');
    }
  };

  if (!command) {
    return (
      <div data-testid="cmdline-example">
        <Typography.Text type="secondary" data-testid="cmdline-placeholder">
          Select a command to see the CLI example
        </Typography.Text>
      </div>
    );
  }

  return (
    <div data-testid="cmdline-example" style={{ position: 'relative' }}>
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
        {cmdLine}
      </pre>
      <Button
        type="text"
        size="small"
        icon={<CopyOutlined />}
        onClick={handleCopy}
        data-testid="cmdline-copy"
        style={{ position: 'absolute', top: 4, right: 4 }}
      />
    </div>
  );
};
