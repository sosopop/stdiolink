import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { MemoryRouter, Route, Routes } from 'react-router-dom';

vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: vi.fn(),
}));

import { DriverDetailPage } from '../Detail';
import { useDriversStore } from '@/stores/useDriversStore';

const mockDriver = {
  id: 'drv1',
  program: '/bin/drv1',
  metaHash: 'abc',
  meta: {
    schemaVersion: '1',
    info: { name: 'Test Driver', version: '1.0', description: 'A test driver' },
    commands: [
      { name: 'add', description: 'Add two numbers', params: [{ name: 'a', type: 'double', required: true }], returns: { type: 'double' } },
    ],
  },
};

function renderDetail(overrides = {}) {
  const state = {
    currentDriver: mockDriver,
    loading: false,
    error: null,
    docsMarkdown: null,
    docsLoading: false,
    fetchDriverDetail: vi.fn(),
    fetchDriverDocs: vi.fn().mockResolvedValue(''),
    ...overrides,
  };
  vi.mocked(useDriversStore).mockImplementation((sel?: any) => sel ? sel(state) : state);
  return render(
    <ConfigProvider>
      <MemoryRouter initialEntries={['/drivers/drv1']}>
        <Routes>
          <Route path="/drivers/:id" element={<DriverDetailPage />} />
        </Routes>
      </MemoryRouter>
    </ConfigProvider>,
  );
}

describe('DriverDetailPage', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders metadata tab', () => {
    renderDetail();
    expect(screen.getByTestId('page-driver-detail')).toBeDefined();
    expect(screen.getByTestId('driver-metadata')).toBeDefined();
  });

  it('shows all tabs', () => {
    renderDetail();
    expect(screen.getByRole('tab', { name: 'Metadata' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Commands' })).toBeDefined();
    expect(screen.getByRole('tab', { name: 'Docs' })).toBeDefined();
  });

  it('shows loading state', () => {
    renderDetail({ loading: true, currentDriver: null });
    expect(screen.getByTestId('detail-loading')).toBeDefined();
  });

  it('shows error state', () => {
    renderDetail({ error: 'Not found', currentDriver: null });
    expect(screen.getByTestId('detail-error')).toBeDefined();
  });

  it('shows not found when no driver', () => {
    renderDetail({ currentDriver: null });
    expect(screen.getByTestId('detail-not-found')).toBeDefined();
  });

  it('renders export buttons', () => {
    renderDetail();
    expect(screen.getByTestId('export-meta-btn')).toBeDefined();
    expect(screen.getByTestId('doc-export-btn')).toBeDefined();
    expect(screen.getByTestId('test-in-lab-btn')).toBeDefined();
  });
});
