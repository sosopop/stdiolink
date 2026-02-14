import { describe, it, expect, vi, beforeEach } from 'vitest';
import apiClient from '../client';
import { instancesApi } from '../instances';

vi.mock('../client', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
  },
}));

describe('instancesApi', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('list() calls GET /instances', async () => {
    const data = { instances: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await instancesApi.list();
    expect(apiClient.get).toHaveBeenCalledWith('/instances', { params: undefined });
    expect(result).toEqual(data);
  });

  it('list() passes projectId filter', async () => {
    const data = { instances: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    await instancesApi.list({ projectId: 'p1' });
    expect(apiClient.get).toHaveBeenCalledWith('/instances', { params: { projectId: 'p1' } });
  });

  it('detail() calls GET /instances/:id', async () => {
    const data = { id: 'i1', projectId: 'p1' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await instancesApi.detail('i1');
    expect(apiClient.get).toHaveBeenCalledWith('/instances/i1');
    expect(result).toEqual(data);
  });

  it('terminate() calls POST /instances/:id/terminate', async () => {
    const data = { terminated: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await instancesApi.terminate('i1');
    expect(apiClient.post).toHaveBeenCalledWith('/instances/i1/terminate');
    expect(result).toEqual(data);
  });

  it('processTree() calls GET /instances/:id/process-tree', async () => {
    const data = { pid: 1234, children: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await instancesApi.processTree('i1');
    expect(apiClient.get).toHaveBeenCalledWith('/instances/i1/process-tree');
    expect(result).toEqual(data);
  });

  it('resources() calls GET /instances/:id/resources', async () => {
    const data = { cpuPercent: 5.0, memoryBytes: 1024 };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await instancesApi.resources('i1');
    expect(apiClient.get).toHaveBeenCalledWith('/instances/i1/resources', { params: undefined });
    expect(result).toEqual(data);
  });

  it('resources() passes includeChildren param', async () => {
    vi.mocked(apiClient.get).mockResolvedValue({ data: {} });
    await instancesApi.resources('i1', { includeChildren: true });
    expect(apiClient.get).toHaveBeenCalledWith('/instances/i1/resources', {
      params: { includeChildren: true },
    });
  });

  it('logs() calls GET /instances/:id/logs', async () => {
    const data = { projectId: 'p1', lines: ['log line'] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await instancesApi.logs('i1', { lines: 100 });
    expect(apiClient.get).toHaveBeenCalledWith('/instances/i1/logs', { params: { lines: 100 } });
    expect(result).toEqual(data);
  });
});
