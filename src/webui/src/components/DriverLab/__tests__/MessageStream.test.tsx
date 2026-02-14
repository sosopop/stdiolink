import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MessageStream } from '../MessageStream';
import type { MessageEntry } from '@/stores/useDriverLabStore';

function makeMessages(count: number): MessageEntry[] {
  return Array.from({ length: count }, (_, i) => ({
    id: `msg-${i}`,
    timestamp: Date.now() + i * 1000,
    direction: i % 2 === 0 ? 'recv' as const : 'send' as const,
    type: i % 2 === 0 ? 'stdout' : 'exec',
    raw: {} as any,
    payload: { value: i },
    expanded: false,
  }));
}

describe('MessageStream', () => {
  it('shows empty state when no messages', () => {
    render(<ConfigProvider><MessageStream messages={[]} autoScroll={true} onToggleMessage={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('empty-messages')).toBeDefined();
  });

  it('renders message entries', () => {
    const msgs = makeMessages(3);
    render(<ConfigProvider><MessageStream messages={msgs} autoScroll={true} onToggleMessage={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('message-stream')).toBeDefined();
    expect(screen.getByTestId('msg-msg-0')).toBeDefined();
    expect(screen.getByTestId('msg-msg-1')).toBeDefined();
    expect(screen.getByTestId('msg-msg-2')).toBeDefined();
  });

  it('renders message stream container', () => {
    render(<ConfigProvider><MessageStream messages={makeMessages(1)} autoScroll={false} onToggleMessage={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('message-stream')).toBeDefined();
  });
});
