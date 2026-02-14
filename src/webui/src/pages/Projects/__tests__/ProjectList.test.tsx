import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

vi.mock('@/stores/useProjectsStore', () => ({
  useProjectsStore: vi.fn(),
}));
vi.mock('@/stores/useServicesStore', () => ({
  useServicesStore: vi.fn(),
}));
vi.mock('@/api/services', () => ({
  servicesApi: { detail: vi.fn() },
}));

import { ProjectsPage } from '../index';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';

const mockProjects = [
  { id: 'p_alpha', name: 'Alpha', serviceId: 's1', enabled: true, valid: true, config: {}, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' },
  { id: 'p_beta', name: 'Beta', serviceId: 's2', enabled: false, valid: true, config: {}, schedule: { type: 'daemon' }, instanceCount: 1, status: 'running' },
];

const defaultProjectsState = {
  projects: mockProjects,
  runtimes: { p_beta: { id: 'p_beta', status: 'running', runningInstances: 1, enabled: true, valid: true, instances: [], schedule: { type: 'daemon', timerActive: false, restartSuppressed: false, consecutiveFailures: 0, shuttingDown: false, autoRestarting: false } } },
  currentProject: null,
  currentRuntime: null,
  loading: false,
  error: null,
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
};

const defaultServicesState = {
  services: [{ id: 's1', name: 'Svc1', version: '1.0', serviceDir: '/d', hasSchema: false, projectCount: 1 }],
  currentService: null,
  loading: false,
  error: null,
  fetchServices: vi.fn(),
  fetchServiceDetail: vi.fn(),
  createService: vi.fn(),
  deleteService: vi.fn(),
  scanServices: vi.fn(),
};

function renderPage(projectOverrides = {}, serviceOverrides = {}) {
  const pState = { ...defaultProjectsState, ...projectOverrides };
  const sState = { ...defaultServicesState, ...serviceOverrides };
  vi.mocked(useProjectsStore).mockImplementation((sel?: any) => sel ? sel(pState) : pState);
  vi.mocked(useServicesStore).mockImplementation((sel?: any) => sel ? sel(sState) : sState);
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <ProjectsPage />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('ProjectsPage (List)', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders project table', () => {
    renderPage();
    expect(screen.getByTestId('project-table')).toBeDefined();
    expect(screen.getByText('p_alpha')).toBeDefined();
    expect(screen.getByText('p_beta')).toBeDefined();
  });

  it('filters by search', () => {
    renderPage();
    fireEvent.change(screen.getByPlaceholderText('Search projects...'), { target: { value: 'alpha' } });
    expect(screen.getByText('p_alpha')).toBeDefined();
    expect(screen.queryByText('p_beta')).toBeNull();
  });

  it('shows loading spinner', () => {
    renderPage({ projects: [], loading: true });
    expect(screen.getByTestId('loading-spinner')).toBeDefined();
  });

  it('calls fetchProjects on mount', () => {
    renderPage();
    expect(defaultProjectsState.fetchProjects).toHaveBeenCalled();
  });

  it('opens create wizard', () => {
    renderPage();
    fireEvent.click(screen.getByTestId('create-btn'));
    expect(screen.getByTestId('create-wizard')).toBeDefined();
  });

  it('shows status indicator for running project', () => {
    renderPage();
    expect(screen.getByTestId('status-p_beta')).toBeDefined();
  });
});
