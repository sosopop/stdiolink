import { describe, it, expect, vi, beforeEach } from 'vitest';
import apiClient from '../client';
import { projectsApi } from '../projects';

vi.mock('../client', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    patch: vi.fn(),
    delete: vi.fn(),
  },
}));

describe('projectsApi', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('list() calls GET /projects', async () => {
    const data = { projects: [], total: 0, page: 1, pageSize: 20 };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await projectsApi.list();
    expect(apiClient.get).toHaveBeenCalledWith('/projects', { params: undefined });
    expect(result).toEqual(data);
  });

  it('list() passes filter params', async () => {
    const data = { projects: [], total: 0, page: 1, pageSize: 20 };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    await projectsApi.list({ serviceId: 's1', status: 'running' });
    expect(apiClient.get).toHaveBeenCalledWith('/projects', {
      params: { serviceId: 's1', status: 'running' },
    });
  });

  it('detail() calls GET /projects/:id', async () => {
    const data = { id: 'p1', name: 'proj' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await projectsApi.detail('p1');
    expect(apiClient.get).toHaveBeenCalledWith('/projects/p1');
    expect(result).toEqual(data);
  });

  it('create() calls POST /projects', async () => {
    const payload = { name: 'new', serviceId: 's1', config: {} };
    const data = { id: 'p2', ...payload };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await projectsApi.create(payload as any);
    expect(apiClient.post).toHaveBeenCalledWith('/projects', payload);
    expect(result).toEqual(data);
  });

  it('update() calls PUT /projects/:id', async () => {
    const payload = { name: 'updated' };
    const data = { id: 'p1', name: 'updated' };
    vi.mocked(apiClient.put).mockResolvedValue({ data });
    const result = await projectsApi.update('p1', payload as any);
    expect(apiClient.put).toHaveBeenCalledWith('/projects/p1', payload);
    expect(result).toEqual(data);
  });

  it('delete() calls DELETE /projects/:id', async () => {
    vi.mocked(apiClient.delete).mockResolvedValue({ data: {} });
    await projectsApi.delete('p1');
    expect(apiClient.delete).toHaveBeenCalledWith('/projects/p1');
  });

  it('validate() calls POST /projects/:id/validate', async () => {
    const data = { valid: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await projectsApi.validate('p1', { port: 8080 });
    expect(apiClient.post).toHaveBeenCalledWith('/projects/p1/validate', { config: { port: 8080 } });
    expect(result).toEqual(data);
  });

  it('start() calls POST /projects/:id/start', async () => {
    const data = { instanceId: 'i1', pid: 1234 };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await projectsApi.start('p1');
    expect(apiClient.post).toHaveBeenCalledWith('/projects/p1/start');
    expect(result).toEqual(data);
  });

  it('stop() calls POST /projects/:id/stop', async () => {
    const data = { stopped: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await projectsApi.stop('p1');
    expect(apiClient.post).toHaveBeenCalledWith('/projects/p1/stop');
    expect(result).toEqual(data);
  });

  it('reload() calls POST /projects/:id/reload', async () => {
    const data = { id: 'p1', name: 'proj' };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await projectsApi.reload('p1');
    expect(apiClient.post).toHaveBeenCalledWith('/projects/p1/reload');
    expect(result).toEqual(data);
  });

  it('runtime() calls GET /projects/:id/runtime', async () => {
    const data = { projectId: 'p1', status: 'running' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await projectsApi.runtime('p1');
    expect(apiClient.get).toHaveBeenCalledWith('/projects/p1/runtime');
    expect(result).toEqual(data);
  });

  it('runtimeBatch() calls GET /projects/runtime', async () => {
    const data = { runtimes: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await projectsApi.runtimeBatch();
    expect(apiClient.get).toHaveBeenCalledWith('/projects/runtime', { params: undefined });
    expect(result).toEqual(data);
  });

  it('runtimeBatch() with ids passes params', async () => {
    const data = { runtimes: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    await projectsApi.runtimeBatch(['p1', 'p2']);
    expect(apiClient.get).toHaveBeenCalledWith('/projects/runtime', {
      params: { ids: 'p1,p2' },
    });
  });

  it('setEnabled() calls PATCH /projects/:id/enabled', async () => {
    const data = { id: 'p1', enabled: false };
    vi.mocked(apiClient.patch).mockResolvedValue({ data });
    const result = await projectsApi.setEnabled('p1', false);
    expect(apiClient.patch).toHaveBeenCalledWith('/projects/p1/enabled', { enabled: false });
    expect(result).toEqual(data);
  });

  it('logs() calls GET /projects/:id/logs', async () => {
    const data = { projectId: 'p1', lines: ['line1'], logPath: '/logs/p1.log' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await projectsApi.logs('p1', { lines: 50 });
    expect(apiClient.get).toHaveBeenCalledWith('/projects/p1/logs', { params: { lines: 50 } });
    expect(result).toEqual(data);
  });
});
