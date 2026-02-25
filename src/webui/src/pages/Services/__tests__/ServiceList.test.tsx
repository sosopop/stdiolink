import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter } from 'react-router-dom';

vi.mock('@/stores/useServicesStore', () => ({
  useServicesStore: vi.fn(),
}));

import { ServicesPage } from '../index';
import { useServicesStore } from '@/stores/useServicesStore';

const mockServices = [
  { id: 'svc_alpha', name: 'Alpha Service', version: '1.0.0', serviceDir: '/d/alpha', hasSchema: true, projectCount: 3 },
  { id: 'svc_beta', name: 'Beta Service', version: '2.0.0', serviceDir: '/d/beta', hasSchema: false, projectCount: 0 },
];

const defaultState = {
  services: mockServices,
  loading: false,
  error: null,
  currentService: null,
  fetchServices: vi.fn(),
  fetchServiceDetail: vi.fn(),
  createService: vi.fn(),
  deleteService: vi.fn(),
  scanServices: vi.fn(),
};

function renderPage(overrides = {}) {
  const state = { ...defaultState, ...overrides };
  vi.mocked(useServicesStore).mockImplementation((selector?: any) =>
    selector ? selector(state) : state,
  );
  return render(
    <ConfigProvider>
      <MemoryRouter>
        <ServicesPage />
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('ServicesPage (List)', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders service table with all services', () => {
    renderPage();
    expect(screen.getByRole('table')).toBeDefined();
    expect(screen.getByText('svc_alpha')).toBeDefined();
    expect(screen.getByText('svc_beta')).toBeDefined();
  });

  it('filters services by search input', () => {
    renderPage();
    const input = screen.getByPlaceholderText('Search services...');
    fireEvent.change(input, { target: { value: 'alpha' } });
    expect(screen.getByText('svc_alpha')).toBeDefined();
    expect(screen.queryByText('svc_beta')).toBeNull();
  });

  it('shows empty table when no services', () => {
    renderPage({ services: [] });
    expect(screen.getByRole('table')).toBeDefined();
  });

  it('shows loading spinner when loading with no data', () => {
    renderPage({ services: [], loading: true });
    expect(screen.getByTestId('loading-spinner')).toBeDefined();
  });

  it('calls fetchServices on mount', () => {
    renderPage();
    expect(defaultState.fetchServices).toHaveBeenCalled();
  });

  it('calls scanServices when scan button clicked', () => {
    renderPage();
    fireEvent.click(screen.getByTestId('scan-btn'));
    expect(defaultState.scanServices).toHaveBeenCalled();
  });

  it('opens create modal when new button clicked', () => {
    renderPage();
    fireEvent.click(screen.getByTestId('create-btn'));
    expect(screen.getByTestId('create-modal')).toBeDefined();
  });
});
