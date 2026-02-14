import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

vi.mock('@/stores/useInstancesStore', () => ({
  useInstancesStore: vi.fn(),
}));
vi.mock('@/stores/useProjectsStore', () => ({
  useProjectsStore: vi.fn(),
}));

import { InstancesPage } from '../index';
import { useInstancesStore } from '@/stores/useInstancesStore';
import { useProjectsStore } from '@/stores/useProjectsStore';

const mockInstances = [
  { id: 'inst-1', projectId: 'p1', serviceId: 's1', pid: 1234, startedAt: '2025-01-01T00:00:00Z', status: 'running' },
  { id: 'inst-2', projectId: 'p2', serviceId: 's1', pid: 5678, startedAt: '2025-01-01T01:00:00Z', status: 'stopped' },
];

function setup(overrides = {}) {
  const state = {
    instances: mockInstances,
    loading: false,
    error: null,
    fetchInstances: vi.fn(),
    terminateInstance: vi.fn(),
    ...overrides,
  };
  vi.mocked(useInstancesStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
  vi.mocked(useProjectsStore).mockImplementation((sel?: any) => {
    const ps = { projects: [], fetchProjects: vi.fn() };
    return sel ? sel(ps) : ps;
  });
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <InstancesPage />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('InstancesPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders page with table', () => {
    setup();
    expect(screen.getByTestId('page-instances')).toBeDefined();
    expect(screen.getByTestId('instances-table')).toBeDefined();
  });

  it('renders search and filters', () => {
    setup();
    expect(screen.getByTestId('instance-search')).toBeDefined();
  });

  it('renders refresh button', () => {
    setup();
    expect(screen.getByTestId('refresh-btn')).toBeDefined();
  });

  it('shows instances in table', () => {
    setup();
    expect(screen.getByText('inst-1')).toBeDefined();
    expect(screen.getByText('inst-2')).toBeDefined();
  });
});
