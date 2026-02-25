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
});
