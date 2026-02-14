import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MessageToolbar } from '../MessageToolbar';
import type { MessageEntry } from '@/stores/useDriverLabStore';

const mockMessages: MessageEntry[] = [
  { id: '1', timestamp: Date.now(), direction: 'recv', type: 'stdout', raw: {} as any, payload: { ok: 1 }, expanded: false },
];

describe('MessageToolbar', () => {
  it('renders clear and export buttons', () => {
    render(<ConfigProvider><MessageToolbar messages={mockMessages} driverId="drv1" onClear={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('clear-btn')).toBeDefined();
    expect(screen.getByTestId('export-btn')).toBeDefined();
  });

  it('calls onClear when clear clicked', () => {
    const onClear = vi.fn();
    render(<ConfigProvider><MessageToolbar messages={mockMessages} driverId="drv1" onClear={onClear} /></ConfigProvider>);
    fireEvent.click(screen.getByTestId('clear-btn'));
    expect(onClear).toHaveBeenCalled();
  });

  it('export button disabled when no messages', () => {
    render(<ConfigProvider><MessageToolbar messages={[]} driverId="drv1" onClear={vi.fn()} /></ConfigProvider>);
    const btn = screen.getByTestId('export-btn').closest('button');
    expect(btn?.disabled).toBe(true);
  });

  it('export button enabled when messages exist', () => {
    render(<ConfigProvider><MessageToolbar messages={mockMessages} driverId="drv1" onClear={vi.fn()} /></ConfigProvider>);
    const btn = screen.getByTestId('export-btn').closest('button');
    expect(btn?.disabled).toBe(false);
  });
});
