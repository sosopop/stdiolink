import React, { useMemo } from 'react';
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

/** 从消息的 raw 字段中提取通用标签胶囊信息 */
interface TagInfo {
  label: string;
  color: string;
}

function extractTags(entry: MessageEntryType): TagInfo[] {
  const tags: TagInfo[] = [];
  const raw = entry.raw as Record<string, unknown>;
  if (!raw || typeof raw !== 'object') return tags;

  // exec 类型显示命令名
  if (entry.type === 'exec' && raw.cmd) {
    tags.push({ label: `cmd: ${raw.cmd}`, color: 'blue' });
  }

  // stdout 类型提取 status 和 code
  if (entry.type === 'stdout') {
    const message = raw.message as Record<string, unknown> | undefined;
    if (message && typeof message === 'object') {
      if (message.status === 'done') {
        tags.push({ label: 'done', color: 'green' });
      } else if (message.status === 'error') {
        tags.push({ label: 'error', color: 'red' });
      } else if (typeof message.status === 'string') {
        tags.push({ label: `status: ${message.status}`, color: 'default' });
      }

      // 错误码
      if (message.code !== undefined && message.code !== null && message.code !== 0) {
        tags.push({ label: `code: ${message.code}`, color: 'red' });
      }

      // 事件类型（长任务推送）
      if (typeof message.event === 'string') {
        tags.push({ label: `event: ${message.event}`, color: 'purple' });
      }
    }
  }

  // error 类型（Server 层错误）
  if (entry.type === 'error') {
    if (raw.code !== undefined && raw.code !== null) {
      tags.push({ label: `code: ${raw.code}`, color: 'red' });
    }
  }

  // driver.started / restarted 显示 PID
  if ((entry.type === 'driver.started' || entry.type === 'driver.restarted') && raw.pid !== undefined) {
    tags.push({ label: `PID: ${raw.pid}`, color: 'cyan' });
  }

  // driver.exited 显示退出码
  if (entry.type === 'driver.exited') {
    const exitCode = raw.exitCode ?? raw.exit_code ?? raw.code;
    if (exitCode !== undefined && exitCode !== null) {
      const isError = exitCode !== 0;
      tags.push({ label: `exit: ${exitCode}`, color: isError ? 'red' : 'green' });
    }
  }

  // cancel 类型
  if (entry.type === 'cancel') {
    tags.push({ label: 'cancelled', color: 'orange' });
  }

  return tags;
}

function previewPayload(payload: unknown): string {
  if (payload === null || payload === undefined) return '';
  if (typeof payload === 'string') return payload;
  return JSON.stringify(payload);
}

export const MessageEntryComponent: React.FC<MessageEntryProps> = ({ entry, onToggle }) => {
  const color = getTypeColor(entry.type, entry.direction);
  const tags = useMemo(() => extractTags(entry), [entry]);

  return (
    <div
      data-testid={`msg-${entry.id}`}
      onClick={() => onToggle(entry.id)}
      style={{
        padding: '8px 12px',
        borderBottom: '1px solid var(--surface-border)',
        cursor: 'pointer',
        fontSize: 13,
        transition: 'background 0.2s',
      }}
      className="message-entry"
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0 }}>
        <span style={{ color, fontWeight: 600, flexShrink: 0 }} data-testid="msg-direction">
          {getDirectionIcon(entry.direction)}
        </span>
        <Typography.Text
          type="secondary"
          style={{ fontSize: 10, fontFamily: 'monospace', flexShrink: 0 }}
          data-testid="msg-timestamp"
        >
          {formatTime(entry.timestamp)}
        </Typography.Text>
        <Tag
          color={color}
          style={{ margin: 0, fontSize: 11, flexShrink: 0 }}
          data-testid="msg-type"
        >
          {entry.type}
        </Tag>
        {tags.map((tag, i) => (
          <Tag
            key={i}
            color={tag.color}
            style={{ margin: 0, fontSize: 11, flexShrink: 0, borderStyle: 'dashed' }}
            data-testid={`msg-tag-${i}`}
          >
            {tag.label}
          </Tag>
        ))}
        {!entry.expanded && (
          <span
            data-testid="msg-preview"
            style={{
              flex: 1,
              minWidth: 0,
              fontSize: 12,
              fontFamily: 'monospace',
              color: 'var(--text-secondary)',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            }}
          >
            {previewPayload(entry.payload)}
          </span>
        )}
      </div>
      {entry.expanded && entry.payload != null && (
        <pre
          onClick={(e) => e.stopPropagation()}
          data-testid="msg-payload"
          style={{
            marginTop: 6,
            padding: 8,
            background: 'var(--bg-body)',
            border: '1px solid var(--surface-border)',
            borderRadius: 6,
            fontSize: 13,
            lineHeight: 1.5,
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
