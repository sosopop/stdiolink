import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter, Route, Routes } from 'react-router-dom';

vi.mock('@/stores/useServicesStore', () => ({
  useServicesStore: vi.fn(),
}));
vi.mock('@/api/projects', () => ({
  projectsApi: { list: vi.fn().mockResolvedValue({ projects: [] }) },
}));
vi.mock('@/api/services', () => ({
  servicesApi: { files: vi.fn().mockResolvedValue({ files: [] }) },
}));
vi.mock('@monaco-editor/react', () => ({
  default: () => <div data-testid="mock-monaco" />,
}));

import { ServiceDetailPage } from '../Detail';
import { useServicesStore } from '@/stores/useServicesStore';

const mockService = {
  id: 'svc_test',
  name: 'Test Service',
  version: '1.0.0',
  serviceDir: '/data/services/svc_test',
  hasSchema: true,
  projectCount: 2,
  manifest: { manifestVersion: '1', id: 'svc_test', name: 'Test Service', version: '1.0.0', description: 'A test', author: 'dev' },
  configSchema: {},
  configSchemaFields: [
    { name: 'host', type: 'string', description: 'Server host', required: true, default: 'localhost' },
    { name: 'port', type: 'int', description: 'Server port', default: 8080 },
  ],
  projects: ['p1', 'p2'],
};

function renderDetail(overrides = {}) {
  const state = {
    currentService: mockService,
    loading: false,
    error: null,
    services: [],
    fetchServices: vi.fn(),
    fetchServiceDetail: vi.fn(),
    createService: vi.fn(),
    deleteService: vi.fn(),
    scanServices: vi.fn(),
    ...overrides,
  };
  vi.mocked(useServicesStore).mockImplementation((selector?: any) =>
    selector ? selector(state) : state,
  );
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={['/services/svc_test']}>
        <Routes>
          <Route path="/services/:id" element={<ServiceDetailPage />} />
        </Routes>
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('ServiceDetailPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders overview tab with service info', () => {
    renderDetail();
    expect(screen.getByTestId('page-service-detail')).toBeDefined();
    expect(screen.getByRole('heading', { name: 'Test Service' })).toBeDefined();
    expect(screen.getByTestId('service-overview')).toBeDefined();
  });

  it('shows tabs for files, schema, projects', () => {
    renderDetail();
    expect(screen.getByRole('tab', { name: 'Files' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Schema' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Projects' })).toBeDefined();
  });

  it('shows loading state', () => {
    renderDetail({ loading: true, currentService: null });
    expect(screen.getByTestId('detail-loading')).toBeDefined();
  });

  it('shows error state', () => {
    renderDetail({ error: 'Not found', currentService: null });
    expect(screen.getByTestId('detail-error')).toBeDefined();
  });

  it('shows not found when no service', () => {
    renderDetail({ currentService: null });
    expect(screen.getByTestId('detail-not-found')).toBeDefined();
  });
});
