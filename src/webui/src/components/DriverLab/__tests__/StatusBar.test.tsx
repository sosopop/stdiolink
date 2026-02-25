import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { StatusBar } from '../StatusBar';
import type { ConnectionState } from '@/stores/useDriverLabStore';

function renderBar(overrides: Partial<ConnectionState> = {}) {
  const connection: ConnectionState = {
    status: 'connected',
    driverId: 'calc',
    runMode: 'keepalive',
    pid: 1234,
    connectedAt: Date.now() - 150000,
    meta: null,
    error: null,
    ...overrides,
  };
  return render(<ConfigProvider><StatusBar connection={connection} /></ConfigProvider>);
}

describe('StatusBar', () => {
  it('renders status bar', () => {
    renderBar();
    expect(screen.getByTestId('status-bar')).toBeDefined();
  });

  it('shows status tag', () => {
    renderBar();
    expect(screen.getByTestId('status-tag').textContent).toBe('Connected');
  });

  it('shows PID when available', () => {
    renderBar({ pid: 5678 });
    expect(screen.getByTestId('status-pid').textContent).toBe('5678');
  });

  it('shows run mode', () => {
    renderBar({ runMode: 'oneshot' });
    expect(screen.getByTestId('status-mode').textContent).toBe('oneshot');
  });

  it('shows uptime when connected', () => {
    renderBar();
    expect(screen.getByTestId('status-uptime')).toBeDefined();
  });

  it('hides PID when null', () => {
    renderBar({ pid: null });
    expect(screen.queryByTestId('status-pid')).toBeNull();
  });
});
