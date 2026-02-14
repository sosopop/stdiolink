import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ActiveInstances } from '../components/ActiveInstances';
import { ConfigProvider } from 'antd';
import type { Instance } from '@/types/instance';

function renderComponent(instances: Instance[] = [], onTerminate = vi.fn()) {
  return render(
    <ConfigProvider>
      <ActiveInstances instances={instances} onTerminate={onTerminate} />
    </ConfigProvider>,
  );
}

describe('ActiveInstances', () => {
  it('renders empty state', () => {
    renderComponent();
    expect(screen.getByTestId('empty-instances')).toBeDefined();
  });

  it('renders instance list', () => {
    const instances = [
      { id: 'i1', projectId: 'p1', status: 'running', pid: 1234 },
    ] as Instance[];
    renderComponent(instances);
    expect(screen.getByText('p1')).toBeDefined();
    expect(screen.getByText('1234')).toBeDefined();
  });

  it('renders status dot for running instance', () => {
    const instances = [
      { id: 'i1', projectId: 'p1', status: 'running', pid: 1234 },
    ] as Instance[];
    renderComponent(instances);
    const dot = screen.getByTestId('status-dot');
    expect(dot.dataset.status).toBe('running');
  });

  it('renders terminate button', () => {
    const instances = [
      { id: 'i1', projectId: 'p1', status: 'running', pid: 1234 },
    ] as Instance[];
    renderComponent(instances);
    expect(screen.getByTestId('terminate-btn')).toBeDefined();
  });
});
