import React from 'react';
import { Typography, Tag } from 'antd';
import type { MessageEntry as MessageEntryType } from '@/stores/useDriverLabStore';

interface MessageEntryProps {
  entry: MessageEntryType;
  onToggle: (id: string) => void;
}

function formatTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 } as Intl.DateTimeFormatOptions);
}

function getDirectionIcon(direction: 'send' | 'recv'): string {
  return direction === 'send' ? '▲' : '▼';
}

function getTypeColor(type: string, direction: 'send' | 'recv'): string {
  if (type === 'error') return '#ff4d4f';
  if (type === 'driver.started' || type === 'driver.exited' || type === 'driver.restarted') return '#8c8c8c';
  return direction === 'send' ? '#1677ff' : '#52c41a';
}

function previewPayload(payload: unknown): string {
  if (payload === null || payload === undefined) return '';
  if (typeof payload === 'string') return payload.length > 80 ? payload.slice(0, 80) + '...' : payload;
  const str = JSON.stringify(payload);
  return str.length > 80 ? str.slice(0, 80) + '...' : str;
}

export const MessageEntryComponent: React.FC<MessageEntryProps> = ({ entry, onToggle }) => {
  const color = getTypeColor(entry.type, entry.direction);

  return (
    <div
      data-testid={`msg-${entry.id}`}
      onClick={() => onToggle(entry.id)}
      style={{
        padding: '6px 10px',
        borderBottom: '1px solid var(--surface-border)',
        cursor: 'pointer',
        fontSize: 13,
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ color, fontWeight: 600 }} data-testid="msg-direction">
          {getDirectionIcon(entry.direction)}
        </span>
        <Typography.Text
          type="secondary"
          style={{ fontSize: 10, fontFamily: 'monospace' }}
          data-testid="msg-timestamp"
        >
          {formatTime(entry.timestamp)}
        </Typography.Text>
        <Tag
          color={color}
          style={{ margin: 0, fontSize: 11 }}
          data-testid="msg-type"
        >
          {entry.type}
        </Tag>
        {!entry.expanded && (
          <Typography.Text
            ellipsis
            style={{ flex: 1, fontSize: 12, fontFamily: 'monospace' }}
            data-testid="msg-preview"
          >
            {previewPayload(entry.payload)}
          </Typography.Text>
        )}
      </div>
      {entry.expanded && entry.payload != null && (
        <pre
          data-testid="msg-payload"
          style={{
            marginTop: 6,
            padding: 8,
            background: 'var(--bg-body)',
            border: '1px solid var(--surface-border)',
            borderRadius: 4,
            fontSize: 12,
            fontFamily: 'monospace',
            whiteSpace: 'pre-wrap',
            wordBreak: 'break-all',
            maxHeight: 300,
            overflow: 'auto',
          }}
        >
          {typeof entry.payload === 'string'
            ? entry.payload
            : JSON.stringify(entry.payload, null, 2)}
        </pre>
      )}
    </div>
  );
};
