import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
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
    const logLines = screen.getAllByTestId('log-line');
    expect(logLines).toHaveLength(2);
  });

  it('shows empty state when no logs', () => {
    renderComponent([]);
    expect(screen.getByTestId('empty-logs')).toBeDefined();
  });

  it('renders level filter', () => {
    renderComponent(['[INFO] test']);
    expect(screen.getByTestId('log-level-filter')).toBeDefined();
  });

  it('renders search input', () => {
    renderComponent(['[INFO] test']);
    expect(screen.getByTestId('log-search')).toBeDefined();
  });
});
