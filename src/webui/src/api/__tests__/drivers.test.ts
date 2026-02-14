import { describe, it, expect, vi, beforeEach } from 'vitest';
import apiClient from '../client';
import { driversApi } from '../drivers';

vi.mock('../client', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}));

describe('driversApi', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('list() calls GET /drivers', async () => {
    const data = { drivers: [{ id: 'd1', name: 'driver1' }] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await driversApi.list();
    expect(apiClient.get).toHaveBeenCalledWith('/drivers');
    expect(result).toEqual(data);
  });

  it('detail() calls GET /drivers/:id', async () => {
    const data = { id: 'd1', name: 'driver1', meta: {} };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await driversApi.detail('d1');
    expect(apiClient.get).toHaveBeenCalledWith('/drivers/d1');
    expect(result).toEqual(data);
  });

  it('scan() calls POST /drivers/scan with empty body', async () => {
    const data = { scanned: 2 };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await driversApi.scan();
    expect(apiClient.post).toHaveBeenCalledWith('/drivers/scan', {});
    expect(result).toEqual(data);
  });

  it('scan() with refreshMeta option', async () => {
    vi.mocked(apiClient.post).mockResolvedValue({ data: {} });
    await driversApi.scan({ refreshMeta: true });
    expect(apiClient.post).toHaveBeenCalledWith('/drivers/scan', { refreshMeta: true });
  });
});
