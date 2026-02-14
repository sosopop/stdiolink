import React from 'react';
import { useTranslation } from 'react-i18next';
import { Tooltip, Space, Typography } from 'antd';
import type { SseStatus } from '@/stores/useEventStreamStore';

interface SseStatusIndicatorProps {
  status: SseStatus;
  lastEventTime: number | null;
  error?: string | null;
}

const statusConfig: Record<SseStatus, { color: string; labelKey: string; animate: boolean }> = {
  connected: { color: '#52c41a', labelKey: 'sse.live', animate: true },
  connecting: { color: '#faad14', labelKey: 'sse.connecting', animate: true },
  reconnecting: { color: '#faad14', labelKey: 'sse.reconnecting', animate: true },
  disconnected: { color: '#ff4d4f', labelKey: 'sse.offline', animate: false },
  error: { color: '#ff4d4f', labelKey: 'sse.error', animate: false },
};

export const SseStatusIndicator: React.FC<SseStatusIndicatorProps> = ({
  status,
  lastEventTime,
  error,
}) => {
  const { t } = useTranslation();
  const config = statusConfig[status];

  const formatLastEvent = (ts: number | null): string => {
    if (!ts) return t('sse.no_events');
    const d = new Date(ts);
    return t('sse.last_event', { time: d.toLocaleTimeString() });
  };

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
          {t(config.labelKey)}
        </Typography.Text>
      </Space>
    </Tooltip>
  );
};
