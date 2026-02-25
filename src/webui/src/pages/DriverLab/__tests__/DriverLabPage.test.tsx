import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: vi.fn(),
}));

vi.mock('@/stores/useDriverLabStore', () => {
  const store = vi.fn();
  store.setState = vi.fn();
  return { useDriverLabStore: store };
});

import { DriverLabPage } from '../index';
import { useDriversStore } from '@/stores/useDriversStore';
import { useDriverLabStore } from '@/stores/useDriverLabStore';

function setupMocks(overrides = {}) {
  const driversState = {
    drivers: [{ id: 'calc', program: 'calc.exe', metaHash: 'abc', name: 'Calculator' }],
    fetchDrivers: vi.fn(),
  };
  vi.mocked(useDriversStore).mockImplementation((sel?: any) => sel ? sel(driversState) : driversState);

  const labState = {
    connection: {
      status: 'disconnected',
      driverId: null,
      runMode: 'oneshot',
      pid: null,
      connectedAt: null,
      meta: null,
      error: null,
    },
    messages: [],
    commands: [],
    selectedCommand: null,
    commandParams: {},
    executing: false,
    autoScroll: true,
    connect: vi.fn(),
    disconnect: vi.fn(),
    execCommand: vi.fn(),
    cancelCommand: vi.fn(),
    selectCommand: vi.fn(),
    setCommandParams: vi.fn(),
    clearMessages: vi.fn(),
    toggleAutoScroll: vi.fn(),
    ...overrides,
  };
  vi.mocked(useDriverLabStore).mockImplementation((sel?: any) => sel ? sel(labState) : labState);

  return { driversState, labState };
}

describe('DriverLabPage', () => {
  beforeEach(() => vi.clearAllMocks());

  function renderPage() {
    return render(
      <ConfigProvider>
        <MemoryRouter>
          <DriverLabPage />
        </MemoryRouter>
      </ConfigProvider>,
    );
  }

  it('renders page layout', () => {
    setupMocks();
    renderPage();
    expect(screen.getByTestId('driverlab-page')).toBeDefined();
    expect(screen.getByTestId('right-panel')).toBeDefined();
  });

  it('renders header with status', () => {
    setupMocks();
    renderPage();
    expect(screen.getByText('Driver Lab')).toBeDefined();
    expect(screen.getByTestId('header-status')).toBeDefined();
  });

  it('renders connection panel', () => {
    setupMocks();
    renderPage();
    expect(screen.getByTestId('connection-panel')).toBeDefined();
  });

  it('renders command panel', () => {
    setupMocks();
    renderPage();
    expect(screen.getByTestId('command-panel')).toBeDefined();
  });

  it('renders message stream', () => {
    setupMocks();
    renderPage();
    expect(screen.getByTestId('message-stream')).toBeDefined();
  });

  it('renders status bar', () => {
    setupMocks();
    renderPage();
    expect(screen.getByTestId('status-bar')).toBeDefined();
  });

  it('fetches drivers on mount', () => {
    const { driversState } = setupMocks();
    renderPage();
    expect(driversState.fetchDrivers).toHaveBeenCalled();
  });
});
