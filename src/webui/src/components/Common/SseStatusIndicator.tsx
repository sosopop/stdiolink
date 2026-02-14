import React from 'react';
import { Tooltip, Space, Typography } from 'antd';
import type { SseStatus } from '@/stores/useEventStreamStore';

interface SseStatusIndicatorProps {
  status: SseStatus;
  lastEventTime: number | null;
  error?: string | null;
}

const statusConfig: Record<SseStatus, { color: string; label: string; animate: boolean }> = {
  connected: { color: '#52c41a', label: 'Live', animate: true },
  connecting: { color: '#faad14', label: 'Connecting', animate: true },
  reconnecting: { color: '#faad14', label: 'Reconnecting', animate: true },
  disconnected: { color: '#ff4d4f', label: 'Offline', animate: false },
  error: { color: '#ff4d4f', label: 'Error', animate: false },
};

function formatLastEvent(ts: number | null): string {
  if (!ts) return 'No events received';
  const d = new Date(ts);
  return `Last event: ${d.toLocaleTimeString()}`;
}

export const SseStatusIndicator: React.FC<SseStatusIndicatorProps> = ({
  status,
  lastEventTime,
  error,
}) => {
  const config = statusConfig[status];
  const tooltipText = error && status === 'error'
    ? error
    : formatLastEvent(lastEventTime);

  return (
    <Tooltip title={tooltipText}>
      <Space size={4} style={{ cursor: 'default' }} data-testid="sse-indicator">
        <span
          data-testid="sse-dot"
          style={{
            display: 'inline-block',
            width: 8,
            height: 8,
            borderRadius: '50%',
            backgroundColor: config.color,
            animation: config.animate ? 'pulse 2s infinite' : undefined,
          }}
        />
        <Typography.Text
          style={{ fontSize: 12 }}
          data-testid="sse-label"
        >
          {config.label}
        </Typography.Text>
      </Space>
    </Tooltip>
  );
};
