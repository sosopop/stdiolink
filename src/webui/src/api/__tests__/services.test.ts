import { describe, it, expect, vi, beforeEach } from 'vitest';
import apiClient from '../client';
import { servicesApi } from '../services';

vi.mock('../client', () => ({
  default: {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
  },
}));

describe('servicesApi', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('list() calls GET /services', async () => {
    const data = { services: [{ id: 's1', name: 'svc' }] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await servicesApi.list();
    expect(apiClient.get).toHaveBeenCalledWith('/services');
    expect(result).toEqual(data);
  });

  it('detail() calls GET /services/:id', async () => {
    const data = { id: 's1', name: 'svc' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await servicesApi.detail('s1');
    expect(apiClient.get).toHaveBeenCalledWith('/services/s1');
    expect(result).toEqual(data);
  });

  it('create() calls POST /services', async () => {
    const payload = { name: 'new-svc', sourceDir: '/path' };
    const data = { id: 's2', name: 'new-svc', created: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await servicesApi.create(payload as any);
    expect(apiClient.post).toHaveBeenCalledWith('/services', payload);
    expect(result).toEqual(data);
  });

  it('delete() calls DELETE /services/:id', async () => {
    vi.mocked(apiClient.delete).mockResolvedValue({ data: {} });
    await servicesApi.delete('s1');
    expect(apiClient.delete).toHaveBeenCalledWith('/services/s1', { params: undefined });
  });

  it('delete() with force calls DELETE /services/:id?force=true', async () => {
    vi.mocked(apiClient.delete).mockResolvedValue({ data: {} });
    await servicesApi.delete('s1', true);
    expect(apiClient.delete).toHaveBeenCalledWith('/services/s1', { params: { force: 'true' } });
  });

  it('scan() calls POST /services/scan', async () => {
    const data = { scanned: 3 };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await servicesApi.scan();
    expect(apiClient.post).toHaveBeenCalledWith('/services/scan', {});
    expect(result).toEqual(data);
  });

  it('scan() with options passes body', async () => {
    const opts = { revalidateProjects: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data: {} });
    await servicesApi.scan(opts);
    expect(apiClient.post).toHaveBeenCalledWith('/services/scan', opts);
  });

  it('files() calls GET /services/:id/files', async () => {
    const data = { serviceId: 's1', serviceDir: '/dir', files: [] };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await servicesApi.files('s1');
    expect(apiClient.get).toHaveBeenCalledWith('/services/s1/files');
    expect(result).toEqual(data);
  });

  it('fileRead() calls GET /services/:id/files/content with path param', async () => {
    const data = { path: 'main.js', content: 'code', size: 4, modifiedAt: '2024-01-01' };
    vi.mocked(apiClient.get).mockResolvedValue({ data });
    const result = await servicesApi.fileRead('s1', 'main.js');
    expect(apiClient.get).toHaveBeenCalledWith('/services/s1/files/content', {
      params: { path: 'main.js' },
    });
    expect(result).toEqual(data);
  });

  it('fileWrite() calls PUT /services/:id/files/content', async () => {
    vi.mocked(apiClient.put).mockResolvedValue({ data: { written: true } });
    await servicesApi.fileWrite('s1', 'main.js', 'new code');
    expect(apiClient.put).toHaveBeenCalledWith(
      '/services/s1/files/content',
      { content: 'new code' },
      { params: { path: 'main.js' } },
    );
  });

  it('fileCreate() calls POST /services/:id/files/content', async () => {
    vi.mocked(apiClient.post).mockResolvedValue({ data: { created: true } });
    await servicesApi.fileCreate('s1', 'new.js', 'content');
    expect(apiClient.post).toHaveBeenCalledWith(
      '/services/s1/files/content',
      { content: 'content' },
      { params: { path: 'new.js' } },
    );
  });

  it('fileDelete() calls DELETE /services/:id/files/content', async () => {
    vi.mocked(apiClient.delete).mockResolvedValue({ data: {} });
    await servicesApi.fileDelete('s1', 'old.js');
    expect(apiClient.delete).toHaveBeenCalledWith('/services/s1/files/content', {
      params: { path: 'old.js' },
    });
  });

  it('validateSchema() calls POST /services/:id/validate-schema', async () => {
    const data = { valid: true };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await servicesApi.validateSchema('s1', { type: 'object' });
    expect(apiClient.post).toHaveBeenCalledWith('/services/s1/validate-schema', {
      schema: { type: 'object' },
    });
    expect(result).toEqual(data);
  });

  it('generateDefaults() calls POST /services/:id/generate-defaults', async () => {
    vi.mocked(apiClient.post).mockResolvedValue({ data: { defaults: {} } });
    await servicesApi.generateDefaults('s1');
    expect(apiClient.post).toHaveBeenCalledWith('/services/s1/generate-defaults');
  });

  it('validateConfig() calls POST /services/:id/validate-config', async () => {
    const data = { valid: false, errors: [{ field: 'port', message: 'required' }] };
    vi.mocked(apiClient.post).mockResolvedValue({ data });
    const result = await servicesApi.validateConfig('s1', { host: 'localhost' });
    expect(apiClient.post).toHaveBeenCalledWith('/services/s1/validate-config', {
      config: { host: 'localhost' },
    });
    expect(result).toEqual(data);
  });
});
