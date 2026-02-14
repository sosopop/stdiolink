import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MessageEntryComponent } from '../MessageEntry';
import type { MessageEntry } from '@/stores/useDriverLabStore';

function makeEntry(overrides: Partial<MessageEntry> = {}): MessageEntry {
  return {
    id: 'msg-1',
    timestamp: new Date('2025-01-15T09:01:02.345Z').getTime(),
    direction: 'recv',
    type: 'stdout',
    raw: { type: 'stdout', message: { ok: 42 } } as any,
    payload: { ok: 42 },
    expanded: false,
    ...overrides,
  };
}

describe('MessageEntryComponent', () => {
  it('renders message with direction icon and type', () => {
    const onToggle = vi.fn();
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry()} onToggle={onToggle} /></ConfigProvider>);
    expect(screen.getByTestId('msg-direction').textContent).toBe('▼');
    expect(screen.getByTestId('msg-type').textContent).toBe('stdout');
  });

  it('shows send direction icon for send messages', () => {
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry({ direction: 'send', type: 'exec' })} onToggle={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('msg-direction').textContent).toBe('▲');
  });

  it('shows preview when collapsed', () => {
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry()} onToggle={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('msg-preview')).toBeDefined();
  });

  it('shows full payload when expanded', () => {
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry({ expanded: true })} onToggle={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('msg-payload')).toBeDefined();
    expect(screen.getByTestId('msg-payload').textContent).toContain('"ok": 42');
  });

  it('calls onToggle when clicked', () => {
    const onToggle = vi.fn();
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry()} onToggle={onToggle} /></ConfigProvider>);
    fireEvent.click(screen.getByTestId('msg-msg-1'));
    expect(onToggle).toHaveBeenCalledWith('msg-1');
  });

  it('shows timestamp', () => {
    render(<ConfigProvider><MessageEntryComponent entry={makeEntry()} onToggle={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('msg-timestamp')).toBeDefined();
  });
});
