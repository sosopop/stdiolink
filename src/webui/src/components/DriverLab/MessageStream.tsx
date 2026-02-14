import React, { useRef, useEffect } from 'react';
import { Typography } from 'antd';
import type { MessageEntry } from '@/stores/useDriverLabStore';
import { MessageEntryComponent } from './MessageEntry';

interface MessageStreamProps {
  messages: MessageEntry[];
  autoScroll: boolean;
  onToggleMessage: (id: string) => void;
}

export const MessageStream: React.FC<MessageStreamProps> = ({
  messages,
  autoScroll,
  onToggleMessage,
}) => {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (autoScroll && bottomRef.current?.scrollIntoView) {
      bottomRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [messages.length, autoScroll]);

  if (messages.length === 0) {
    return (
      <div data-testid="message-stream" style={{ padding: 24, textAlign: 'center' }}>
        <Typography.Text type="secondary" data-testid="empty-messages">
          No messages yet
        </Typography.Text>
      </div>
    );
  }

  return (
    <div
      data-testid="message-stream"
      style={{ flex: 1, overflow: 'auto', minHeight: 0 }}
    >
      {messages.map((msg) => (
        <MessageEntryComponent
          key={msg.id}
          entry={msg}
          onToggle={onToggleMessage}
        />
      ))}
      <div ref={bottomRef} />
    </div>
  );
};
