import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';
import { InstancesTable } from '@/components/Instances/InstancesTable';

const mockInstances = [
  { id: 'inst-1', projectId: 'p1', serviceId: 's1', pid: 1234, startedAt: '2025-01-01T00:00:00Z', status: 'running' },
  { id: 'inst-2', projectId: 'p2', serviceId: 's1', pid: 5678, startedAt: '2025-01-01T01:00:00Z', status: 'stopped' },
];

function renderTable(instances = mockInstances, onTerminate = vi.fn()) {
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <InstancesTable instances={instances} onTerminate={onTerminate} />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('InstancesTable', () => {
  it('renders instances', () => {
    renderTable();
    expect(screen.getByTestId('instances-table')).toBeDefined();
    expect(screen.getByText('inst-1')).toBeDefined();
    expect(screen.getByText('inst-2')).toBeDefined();
  });

  it('shows PID column', () => {
    renderTable();
    expect(screen.getByText('1234')).toBeDefined();
    expect(screen.getByText('5678')).toBeDefined();
  });

  it('renders detail buttons', () => {
    renderTable();
    expect(screen.getByTestId('detail-inst-1')).toBeDefined();
    expect(screen.getByTestId('detail-inst-2')).toBeDefined();
  });

  it('renders terminate buttons', () => {
    renderTable();
    expect(screen.getByTestId('terminate-inst-1')).toBeDefined();
    expect(screen.getByTestId('terminate-inst-2')).toBeDefined();
  });

  it('renders empty table', () => {
    renderTable([]);
    expect(screen.getByTestId('instances-table')).toBeDefined();
  });
});
