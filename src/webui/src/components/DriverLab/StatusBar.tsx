import React from 'react';
import { Space, Tag, Typography } from 'antd';
import type { ConnectionState } from '@/stores/useDriverLabStore';

interface StatusBarProps {
  connection: ConnectionState;
}

function formatUptime(connectedAt: number | null): string {
  if (!connectedAt) return '--';
  const diff = Math.floor((Date.now() - connectedAt) / 1000);
  const m = Math.floor(diff / 60);
  const s = diff % 60;
  return `${m}m${s.toString().padStart(2, '0')}s`;
}

const statusColorMap: Record<string, string> = {
  connected: 'green',
  connecting: 'blue',
  disconnected: 'default',
  error: 'red',
};

export const StatusBar: React.FC<StatusBarProps> = ({ connection }) => {
  return (
    <div
      data-testid="status-bar"
      style={{
        padding: '6px 16px',
        borderTop: '1px solid var(--border-secondary, #303030)',
        display: 'flex',
        alignItems: 'center',
        gap: 16,
        fontSize: 12,
      }}
    >
      <Space size={4}>
        <Typography.Text type="secondary">Status:</Typography.Text>
        <Tag color={statusColorMap[connection.status]} data-testid="status-tag">
          {connection.status}
        </Tag>
      </Space>
      {connection.pid && (
        <Space size={4}>
          <Typography.Text type="secondary">PID:</Typography.Text>
          <Typography.Text data-testid="status-pid">{connection.pid}</Typography.Text>
        </Space>
      )}
      <Space size={4}>
        <Typography.Text type="secondary">Mode:</Typography.Text>
        <Typography.Text data-testid="status-mode">{connection.runMode}</Typography.Text>
      </Space>
      {connection.connectedAt && (
        <Space size={4}>
          <Typography.Text type="secondary">Uptime:</Typography.Text>
          <Typography.Text data-testid="status-uptime">
            {formatUptime(connection.connectedAt)}
          </Typography.Text>
        </Space>
      )}
    </div>
  );
};
