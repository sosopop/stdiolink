import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@/api/services', () => ({
  servicesApi: {
    files: vi.fn(),
    fileRead: vi.fn(),
    fileWrite: vi.fn(),
    fileCreate: vi.fn(),
    fileDelete: vi.fn(),
  },
}));
vi.mock('@monaco-editor/react', () => ({
  default: (props: any) => (
    <div data-testid="mock-monaco">
      <textarea
        data-testid="mock-editor-textarea"
        value={props.value}
        onChange={(e) => props.onChange?.(e.target.value)}
      />
    </div>
  ),
}));

import { ServiceFiles } from '../components/ServiceFiles';
import { servicesApi } from '@/api/services';

const mockFiles = [
  { name: 'manifest.json', path: 'manifest.json', size: 200, type: 'file', modifiedAt: '2024-01-01' },
  { name: 'index.js', path: 'index.js', size: 500, type: 'file', modifiedAt: '2024-01-01' },
  { name: 'helper.js', path: 'helper.js', size: 100, type: 'file', modifiedAt: '2024-01-01' },
];

function renderComponent() {
  vi.mocked(servicesApi.files).mockResolvedValue({ serviceId: 's1', serviceDir: '/d', files: mockFiles });
  return render(
    <ConfigProvider>
      <ServiceFiles serviceId="s1" />
    </ConfigProvider>,
  );
}

async function waitForTreeNodes() {
  await waitFor(() => expect(screen.getByTestId('file-node-helper.js')).toBeDefined());
}

describe('ServiceFiles', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders file tree after loading', async () => {
    renderComponent();
    await waitForTreeNodes();
    expect(screen.getByTestId('file-tree')).toBeDefined();
  });

  it('loads file content when file is selected', async () => {
    vi.mocked(servicesApi.fileRead).mockResolvedValue({ path: 'helper.js', content: '// hello', size: 8, modifiedAt: '2024-01-01' });
    renderComponent();
    await waitForTreeNodes();

    fireEvent.click(screen.getByTestId('file-node-helper.js'));
    await waitFor(() => {
      expect(servicesApi.fileRead).toHaveBeenCalledWith('s1', 'helper.js');
    });
  });

  it('calls fileWrite on save', async () => {
    vi.mocked(servicesApi.fileRead).mockResolvedValue({ path: 'helper.js', content: '// hello', size: 8, modifiedAt: '2024-01-01' });
    vi.mocked(servicesApi.fileWrite).mockResolvedValue({});
    renderComponent();
    await waitForTreeNodes();

    fireEvent.click(screen.getByTestId('file-node-helper.js'));
    await waitFor(() => expect(screen.getByTestId('mock-monaco')).toBeDefined());
  });

  it('calls fileCreate when create button clicked', async () => {
    vi.mocked(servicesApi.fileCreate).mockResolvedValue({});
    renderComponent();
    await waitForTreeNodes();

    fireEvent.click(screen.getByTestId('create-file-btn'));
    await waitFor(() => {
      expect(servicesApi.fileCreate).toHaveBeenCalledWith('s1', 'new_file.js', '');
    });
  });

  it('calls fileDelete when delete confirmed', async () => {
    vi.mocked(servicesApi.fileRead).mockResolvedValue({ path: 'helper.js', content: '// hello', size: 8, modifiedAt: '2024-01-01' });
    vi.mocked(servicesApi.fileDelete).mockResolvedValue(undefined as any);
    renderComponent();
    await waitForTreeNodes();

    fireEvent.click(screen.getByTestId('file-node-helper.js'));
    await waitFor(() => expect(screen.getByTestId('delete-file-btn')).toBeDefined());
  });

  it('disables delete for protected files', async () => {
    vi.mocked(servicesApi.fileRead).mockResolvedValue({ path: 'manifest.json', content: '{}', size: 2, modifiedAt: '2024-01-01' });
    renderComponent();
    await waitFor(() => expect(screen.getByTestId('file-node-manifest.json')).toBeDefined());

    fireEvent.click(screen.getByTestId('file-node-manifest.json'));
    await waitFor(() => {
      const btn = screen.getByTestId('delete-file-btn');
      expect(btn.closest('button')?.disabled).toBe(true);
    });
  });

  it('shows file size warning for large files', async () => {
    vi.mocked(servicesApi.fileRead).mockResolvedValue({ path: 'helper.js', content: '', size: 2 * 1024 * 1024, modifiedAt: '2024-01-01' });
    renderComponent();
    await waitForTreeNodes();

    fireEvent.click(screen.getByTestId('file-node-helper.js'));
    await waitFor(() => expect(screen.getByTestId('file-size-warning')).toBeDefined());
  });
});
