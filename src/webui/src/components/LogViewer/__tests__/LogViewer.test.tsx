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

  it('keeps timestamp column and strips runtime stderr wrapper for structured json logs', () => {
    renderComponent([
      '2026-03-18T07:29:33.606Z | [stderr] {"ts":"2026-03-18T07:29:33.606Z","level":"info","msg":"stdout","fields":{"data":"hello"}}',
    ]);

    expect(screen.getByText('2026-03-18T07:29:33.606Z')).toBeDefined();
    expect(screen.getByText('INFO')).toBeDefined();
    expect(
      screen.getByText(
        '{"ts":"2026-03-18T07:29:33.606Z","level":"info","msg":"stdout","fields":{"data":"hello"}}',
      ),
    ).toBeDefined();
    expect(screen.queryByText(/\[stderr\]/)).toBeNull();
    expect(screen.queryByText(/Z \|/)).toBeNull();
  });

  it('strips nested runtime and qt warning wrappers before structured json logs', () => {
    renderComponent([
      '2026-03-18T07:29:33.606Z | [stderr] Warning: {"ts":"2026-03-18T07:29:33.500Z","level":"warn","msg":"stderr","fields":{"data":"oops"}}',
    ]);

    expect(screen.getByText('2026-03-18T07:29:33.500Z')).toBeDefined();
    expect(screen.getByText('WARN')).toBeDefined();
    expect(
      screen.getByText(
        '{"ts":"2026-03-18T07:29:33.500Z","level":"warn","msg":"stderr","fields":{"data":"oops"}}',
      ),
    ).toBeDefined();
    expect(screen.queryByText(/\[stderr\]/)).toBeNull();
    expect(screen.queryByText(/Warning:/)).toBeNull();
    expect(screen.queryByText(/Z \|/)).toBeNull();
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
