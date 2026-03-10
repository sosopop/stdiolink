import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter, Route, Routes } from 'react-router-dom';

vi.mock('@/stores/useProjectsStore', () => ({
  useProjectsStore: vi.fn(),
}));
vi.mock('@/stores/useServicesStore', () => ({
  useServicesStore: vi.fn(),
}));
vi.mock('@/stores/useDashboardStore', () => ({
  useDashboardStore: vi.fn(),
}));
vi.mock('@/api/instances', () => ({
  instancesApi: { list: vi.fn().mockResolvedValue({ instances: [] }) },
}));
vi.mock('@/api/projects', () => ({
  projectsApi: { logs: vi.fn().mockResolvedValue({ projectId: 'p1', lines: [], logPath: '' }) },
}));

import { ProjectDetailPage } from '../Detail';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';
import { useDashboardStore } from '@/stores/useDashboardStore';

const mockProject = {
  id: 'p1',
  name: 'Test Project',
  serviceId: 's1',
  enabled: true,
  valid: true,
  config: { host: 'localhost' },
  schedule: { type: 'manual' },
  instanceCount: 0,
  status: 'stopped',
};

const mockRuntime = {
  id: 'p1',
  enabled: true,
  valid: true,
  status: 'stopped',
  runningInstances: 0,
  instances: [],
  schedule: { type: 'manual', timerActive: false, restartSuppressed: false, consecutiveFailures: 0, shuttingDown: false, autoRestarting: false },
};

function renderDetail(projectOverrides = {}, serviceOverrides = {}, dashboardOverrides = {}) {
  const pState = {
    currentProject: mockProject,
    currentRuntime: mockRuntime,
    loading: false,
    error: null,
    projects: [],
    runtimes: {},
    fetchProjects: vi.fn(),
    fetchProjectDetail: vi.fn(),
    fetchRuntimes: vi.fn(),
    fetchRuntime: vi.fn(),
    createProject: vi.fn(),
    updateProject: vi.fn(),
    deleteProject: vi.fn(),
    startProject: vi.fn(),
    stopProject: vi.fn(),
    reloadProject: vi.fn(),
    setEnabled: vi.fn(),
    ...projectOverrides,
  };
  const sState = {
    services: [],
    currentService: null,
    loading: false,
    error: null,
    fetchServices: vi.fn(),
    fetchServiceDetail: vi.fn(),
    createService: vi.fn(),
    deleteService: vi.fn(),
    scanServices: vi.fn(),
    ...serviceOverrides,
  };
  const dState = {
    serverStatus: { dataRoot: '/data' },
    instances: [],
    events: [],
    loading: false,
    error: null,
    connected: false,
    fetchServerStatus: vi.fn(),
    fetchInstances: vi.fn(),
    addEvent: vi.fn(),
    setConnected: vi.fn(),
    ...dashboardOverrides,
  };
  vi.mocked(useProjectsStore).mockImplementation((sel?: any) => sel ? sel(pState) : pState);
  vi.mocked(useServicesStore).mockImplementation((sel?: any) => sel ? sel(sState) : sState);
  vi.mocked(useDashboardStore).mockImplementation((sel?: any) => sel ? sel(dState) : dState);
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={['/projects/p1']}>
        <Routes>
          <Route path="/projects/:id" element={<ProjectDetailPage />} />
        </Routes>
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('ProjectDetailPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders overview tab', () => {
    renderDetail({}, { currentService: { serviceDir: '/data/services/s1', configSchemaFields: [] } });
    expect(screen.getByTestId('page-project-detail')).toBeDefined();
    expect(screen.getByTestId('project-overview')).toBeDefined();
    expect(screen.queryByTestId('project-config-test-commands')).toBeNull();
  });

  it('shows config test commands in config tab', async () => {
    renderDetail({}, { currentService: { serviceDir: '/data/services/s1', configSchemaFields: [] } });
    fireEvent.click(screen.getByRole('tab', { name: 'Config' }));
    await waitFor(() => {
      expect(screen.getByTestId('project-config-test-commands')).toBeDefined();
    });
    expect(screen.queryByTestId('export-config-btn')).toBeNull();
    expect(screen.getByTestId('project-config-test-commands').textContent)
      .toContain('stdiolink_service "services/s1" --data-root="." --config-file="projects/p1/param.json"');
  });

  it('shows all tabs', () => {
    renderDetail();
    expect(screen.getByRole('tab', { name: 'Overview' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Config' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Instances' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Logs' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Schedule' })).toBeDefined();
  });

  it('shows loading state', () => {
    renderDetail({ loading: true, currentProject: null });
    expect(screen.getByTestId('detail-loading')).toBeDefined();
  });

  it('shows error state', () => {
    renderDetail({ error: 'Not found', currentProject: null });
    expect(screen.getByTestId('detail-error')).toBeDefined();
  });

  it('shows not found when no project', () => {
    renderDetail({ currentProject: null });
    expect(screen.getByTestId('detail-not-found')).toBeDefined();
  });
});
