import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ConnectionPanel } from '../ConnectionPanel';

const mockDrivers = [
  { id: 'calc', program: 'calc.exe', metaHash: 'abc', name: 'Calculator' },
  { id: 'ping', program: 'ping.exe', metaHash: 'def', name: 'Ping' },
];

function renderPanel(overrides = {}) {
  const props = {
    drivers: mockDrivers,
    status: 'disconnected' as const,
    onConnect: vi.fn(),
    onDisconnect: vi.fn(),
    ...overrides,
  };
  return { ...render(<ConfigProvider><ConnectionPanel {...props} /></ConfigProvider>), props };
}

describe('ConnectionPanel', () => {
  it('renders connection panel', () => {
    renderPanel();
    expect(screen.getByTestId('connection-panel')).toBeDefined();
  });

  it('renders driver select', () => {
    renderPanel();
    expect(screen.getByTestId('driver-select')).toBeDefined();
  });

  it('renders run mode radio', () => {
    renderPanel();
    expect(screen.getByTestId('runmode-radio')).toBeDefined();
    expect(screen.getByText('OneShot')).toBeDefined();
    expect(screen.getByText('KeepAlive')).toBeDefined();
  });

  it('renders args input', () => {
    renderPanel();
    expect(screen.getByTestId('args-input')).toBeDefined();
  });

  it('shows connect button when disconnected', () => {
    renderPanel();
    expect(screen.getByTestId('connect-btn')).toBeDefined();
  });

  it('shows disconnect button when connected', () => {
    const { props } = renderPanel({ status: 'connected' });
    expect(screen.getByTestId('disconnect-btn')).toBeDefined();
    fireEvent.click(screen.getByTestId('disconnect-btn'));
    expect(props.onDisconnect).toHaveBeenCalled();
  });

  it('shows loading state when connecting', () => {
    renderPanel({ status: 'connecting' });
    expect(screen.getByTestId('connect-btn')).toBeDefined();
    expect(screen.getByText('Connecting...')).toBeDefined();
  });
});
