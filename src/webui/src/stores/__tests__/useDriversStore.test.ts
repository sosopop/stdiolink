import { describe, it, expect, vi, beforeEach } from 'vitest';
import { driversApi } from '@/api/drivers';
import { useDriversStore } from '../useDriversStore';

vi.mock('@/api/drivers', () => ({
  driversApi: {
    list: vi.fn(),
    detail: vi.fn(),
    scan: vi.fn(),
    docs: vi.fn(),
  },
}));

const mockDriver = { id: 'drv1', program: '/usr/bin/drv1', metaHash: 'abc', name: 'Driver One', version: '1.0' };

describe('useDriversStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useDriversStore.setState({
      drivers: [],
      currentDriver: null,
      docsMarkdown: null,
      docsLoading: false,
      loading: false,
      error: null,
    });
  });

  it('fetchDrivers updates list', async () => {
    vi.mocked(driversApi.list).mockResolvedValue({ drivers: [mockDriver] });
    await useDriversStore.getState().fetchDrivers();
    expect(useDriversStore.getState().drivers).toHaveLength(1);
  });

  it('fetchDrivers sets error on failure', async () => {
    vi.mocked(driversApi.list).mockRejectedValue({ error: 'Network error' });
    await useDriversStore.getState().fetchDrivers();
    expect(useDriversStore.getState().error).toBe('Network error');
  });

  it('fetchDriverDetail updates currentDriver', async () => {
    const detail = { ...mockDriver, meta: { schemaVersion: '1', info: { name: 'D1', version: '1.0' }, commands: [] } };
    vi.mocked(driversApi.detail).mockResolvedValue(detail);
    await useDriversStore.getState().fetchDriverDetail('drv1');
    expect(useDriversStore.getState().currentDriver?.id).toBe('drv1');
  });

  it('fetchDriverDocs updates docsMarkdown', async () => {
    vi.mocked(driversApi.docs).mockResolvedValue('# Driver Docs');
    const content = await useDriversStore.getState().fetchDriverDocs('drv1');
    expect(content).toBe('# Driver Docs');
    expect(useDriversStore.getState().docsMarkdown).toBe('# Driver Docs');
  });

  it('fetchDriverDocs with html format does not set docsMarkdown', async () => {
    vi.mocked(driversApi.docs).mockResolvedValue('<h1>Docs</h1>');
    const content = await useDriversStore.getState().fetchDriverDocs('drv1', 'html');
    expect(content).toBe('<h1>Docs</h1>');
    expect(useDriversStore.getState().docsMarkdown).toBeNull();
  });

  it('fetchDriverDocs sets error on failure', async () => {
    vi.mocked(driversApi.docs).mockRejectedValue({ error: 'Not found' });
    await expect(useDriversStore.getState().fetchDriverDocs('drv1')).rejects.toBeDefined();
    expect(useDriversStore.getState().docsMarkdown).toBeNull();
  });

  it('scanDrivers calls scan and refreshes list', async () => {
    vi.mocked(driversApi.scan).mockResolvedValue({});
    vi.mocked(driversApi.list).mockResolvedValue({ drivers: [mockDriver] });
    await useDriversStore.getState().scanDrivers();
    expect(driversApi.scan).toHaveBeenCalledWith({ refreshMeta: true });
    expect(useDriversStore.getState().drivers).toHaveLength(1);
  });
});
