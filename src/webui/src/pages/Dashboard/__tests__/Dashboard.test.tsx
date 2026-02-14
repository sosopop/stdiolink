import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

// Mock hooks and stores before importing the component
vi.mock('@/stores/useDashboardStore', () => ({
  useDashboardStore: vi.fn(),
}));
vi.mock('@/hooks/usePolling', () => ({
  usePolling: vi.fn(),
}));
vi.mock('@/hooks/useEventStream', () => ({
  useEventStream: vi.fn(),
}));

import { DashboardPage } from '../index';
import { useDashboardStore } from '@/stores/useDashboardStore';

const defaultState = {
  serverStatus: {
    status: 'running',
    version: '1.0.0',
    uptimeMs: 3600000,
    startedAt: '2024-01-01',
    host: '0.0.0.0',
    port: 8080,
    dataRoot: '/data',
    serviceProgram: 'stdiolink_service',
    counts: {
      services: 3,
      projects: { total: 5, valid: 4, invalid: 1, enabled: 3, disabled: 2 },
      instances: { total: 2, running: 1 },
      drivers: 4,
    },
    system: { platform: 'win32', cpuCores: 8 },
  },
  instances: [],
  events: [],
  loading: false,
  error: null,
  connected: true,
  fetchServerStatus: vi.fn(),
  fetchInstances: vi.fn(),
  addEvent: vi.fn(),
  setConnected: vi.fn(),
};

function renderDashboard(stateOverrides = {}) {
  vi.mocked(useDashboardStore).mockImplementation((selector: any) => {
    const state = { ...defaultState, ...stateOverrides };
    return selector ? selector(state) : state;
  });

  return render(
    <ConfigProvider>
      <MemoryRouter>
        <DashboardPage />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('DashboardPage', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('renders KPI section, instances, and event feed', () => {
    renderDashboard();
    expect(screen.getByTestId('kpi-section')).toBeDefined();
    expect(screen.getByTestId('active-instances')).toBeDefined();
    expect(screen.getByTestId('event-feed')).toBeDefined();
  });

  it('renders server indicator', () => {
    renderDashboard();
    expect(screen.getByTestId('server-indicator')).toBeDefined();
  });

  it('shows error alert when error is set', () => {
    renderDashboard({ error: 'Connection failed' });
    expect(screen.getByTestId('dashboard-error')).toBeDefined();
  });
});
