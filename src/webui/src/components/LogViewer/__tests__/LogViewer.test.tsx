import { describe, it, expect } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { LogViewer } from '../LogViewer';

function renderComponent(lines: string[] = []) {
  return render(
    <ConfigProvider>
      <LogViewer lines={lines} />
    </ConfigProvider>,
  );
}

describe('LogViewer', () => {
  it('renders log lines', () => {
    renderComponent(['[INFO] Server started', '[ERROR] Connection failed']);
    expect(screen.getByText(/\[INFO\] Server started/)).toBeDefined();
    expect(screen.getByText(/\[ERROR\] Connection failed/)).toBeDefined();
  });

  it('shows empty state when no logs', () => {
    renderComponent([]);
    expect(screen.getByText('No logs available')).toBeDefined();
  });

  it('renders level filter', () => {
    renderComponent(['[INFO] test']);
    expect(screen.getByText('ALL')).toBeDefined();
  });

  it('renders search input', () => {
    renderComponent(['[INFO] test']);
    expect(screen.getByPlaceholderText('Search logs...')).toBeDefined();
  });

  it('pauses auto-scroll when log window is scrolled away from the bottom', async () => {
    renderComponent(['[INFO] first', '[INFO] second']);

    const logWindow = screen.getByTestId('log-viewer-window');

    Object.defineProperty(logWindow, 'scrollHeight', { configurable: true, value: 1000 });
    Object.defineProperty(logWindow, 'clientHeight', { configurable: true, value: 200 });
    Object.defineProperty(logWindow, 'scrollTop', { configurable: true, writable: true, value: 100 });

    fireEvent.scroll(logWindow);

    expect(await screen.findByTestId('log-viewer-scroll-bottom')).toBeDefined();
  });
});
