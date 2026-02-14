import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: vi.fn(),
}));

import { DriversPage } from '../index';
import { useDriversStore } from '@/stores/useDriversStore';

const mockDrivers = [
  { id: 'drv1', program: '/bin/drv1', metaHash: 'a', name: 'Driver One', version: '1.0' },
  { id: 'drv2', program: '/bin/drv2', metaHash: 'b', name: 'Driver Two', version: '2.0' },
];

function setup(overrides = {}) {
  const state = {
    drivers: mockDrivers,
    loading: false,
    error: null,
    fetchDrivers: vi.fn(),
    scanDrivers: vi.fn(),
    ...overrides,
  };
  vi.mocked(useDriversStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <DriversPage />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('DriversPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders page with table', () => {
    setup();
    expect(screen.getByTestId('page-drivers')).toBeDefined();
    expect(screen.getByTestId('drivers-table')).toBeDefined();
  });

  it('renders search input', () => {
    setup();
    expect(screen.getByTestId('driver-search')).toBeDefined();
  });

  it('renders scan and refresh buttons', () => {
    setup();
    expect(screen.getByTestId('scan-btn')).toBeDefined();
    expect(screen.getByTestId('refresh-btn')).toBeDefined();
  });

  it('shows drivers in table', () => {
    setup();
    expect(screen.getByText('drv1')).toBeDefined();
    expect(screen.getByText('drv2')).toBeDefined();
  });

  it('shows empty state when no drivers', () => {
    setup({ drivers: [] });
    expect(screen.getByTestId('empty-drivers')).toBeDefined();
  });
});
