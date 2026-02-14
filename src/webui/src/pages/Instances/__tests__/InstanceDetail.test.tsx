import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter, Route, Routes } from 'react-router-dom';

vi.mock('@/stores/useInstancesStore', () => ({
  useInstancesStore: vi.fn(),
}));

import { InstanceDetailPage } from '../Detail';
import { useInstancesStore } from '@/stores/useInstancesStore';

const mockInstance = {
  id: 'inst-1',
  projectId: 'p1',
  serviceId: 's1',
  pid: 1234,
  startedAt: '2025-01-01T00:00:00Z',
  status: 'running',
};

function renderDetail(overrides = {}) {
  const state = {
    currentInstance: mockInstance,
    processTree: null,
    resources: [],
    resourceHistory: [],
    logs: [],
    loading: false,
    error: null,
    fetchInstanceDetail: vi.fn(),
    fetchProcessTree: vi.fn(),
    fetchResources: vi.fn(),
    fetchLogs: vi.fn(),
    terminateInstance: vi.fn(),
    ...overrides,
  };
  vi.mocked(useInstancesStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={['/instances/inst-1']}>
        <Routes>
          <Route path="/instances/:id" element={<InstanceDetailPage />} />
        </Routes>
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('InstanceDetailPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders overview tab', () => {
    renderDetail();
    expect(screen.getByTestId('page-instance-detail')).toBeDefined();
    expect(screen.getByTestId('instance-overview')).toBeDefined();
  });

  it('shows all tabs', () => {
    renderDetail();
    expect(screen.getByRole('tab', { name: 'Overview' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Process Tree' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Resources' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Logs' })).toBeDefined();
  });

  it('shows loading state', () => {
    renderDetail({ loading: true, currentInstance: null });
    expect(screen.getByTestId('detail-loading')).toBeDefined();
  });

  it('shows error state', () => {
    renderDetail({ error: 'Not found', currentInstance: null });
    expect(screen.getByTestId('detail-error')).toBeDefined();
  });

  it('shows not found when no instance', () => {
    renderDetail({ currentInstance: null });
    expect(screen.getByTestId('detail-not-found')).toBeDefined();
  });
});
