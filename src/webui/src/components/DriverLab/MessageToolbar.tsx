import React from 'react';
import { Button, Space } from 'antd';
import { DeleteOutlined, DownloadOutlined } from '@ant-design/icons';
import type { MessageEntry } from '@/stores/useDriverLabStore';

interface MessageToolbarProps {
  messages: MessageEntry[];
  driverId: string | null;
  onClear: () => void;
}

function exportMessages(messages: MessageEntry[], driverId: string | null): void {
  const data = messages.map((m) => ({
    timestamp: new Date(m.timestamp).toISOString(),
    direction: m.direction,
    type: m.type,
    payload: m.payload,
  }));
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `driverlab_${driverId ?? 'unknown'}_${Date.now()}.json`;
  a.click();
  URL.revokeObjectURL(url);
}

export const MessageToolbar: React.FC<MessageToolbarProps> = ({
  messages,
  driverId,
  onClear,
}) => {
  return (
    <div data-testid="message-toolbar" style={{ padding: '8px 0' }}>
      <Space>
        <Button
          size="small"
          icon={<DeleteOutlined />}
          onClick={onClear}
          data-testid="clear-btn"
        >
          Clear
        </Button>
        <Button
          size="small"
          icon={<DownloadOutlined />}
          onClick={() => exportMessages(messages, driverId)}
          disabled={messages.length === 0}
          data-testid="export-btn"
        >
          Export JSON
        </Button>
      </Space>
    </div>
  );
};
