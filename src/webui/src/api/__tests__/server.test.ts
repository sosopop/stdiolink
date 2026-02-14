import { describe, it, expect, vi, beforeEach } from 'vitest';
import apiClient from '../client';
import { serverApi } from '../server';

vi.mock('../client', () => ({
  default: {
    get: vi.fn(),
  },
}));

describe('serverApi', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('status() calls GET /server/status', async () => {
    const data = {
      status: 'running',
      version: '1.0.0',
      uptimeMs: 60000,
      startedAt: '2024-01-01T00:00:00Z',
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
    };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await serverApi.status();
    expect(apiClient.get).toHaveBeenCalledWith('/server/status');
    expect(result).toEqual(data);
    expect(result.counts.services).toBe(3);
    expect(result.system.platform).toBe('win32');
  });
});
