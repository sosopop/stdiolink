import { describe, it, expect, vi, beforeEach } from 'vitest';
import { servicesApi } from '@/api/services';
import { useServicesStore } from '../useServicesStore';

vi.mock('@/api/services', () => ({
  servicesApi: {
    list: vi.fn(),
    detail: vi.fn(),
    create: vi.fn(),
    delete: vi.fn(),
    scan: vi.fn(),
  },
}));

describe('useServicesStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useServicesStore.setState({
      services: [],
      currentService: null,
      loading: false,
      error: null,
    });
  });

  it('fetchServices success', async () => {
    const mockServices = [{ id: 's1', name: 'Svc1', version: '1.0', serviceDir: '/d', hasSchema: true, projectCount: 2 }];
    vi.mocked(servicesApi.list).mockResolvedValue({ services: mockServices });

    await useServicesStore.getState().fetchServices();

    expect(useServicesStore.getState().services).toEqual(mockServices);
    expect(useServicesStore.getState().loading).toBe(false);
    expect(useServicesStore.getState().error).toBeNull();
  });

  it('fetchServices failure', async () => {
    vi.mocked(servicesApi.list).mockRejectedValue({ error: 'Network error' });

    await useServicesStore.getState().fetchServices();

    expect(useServicesStore.getState().services).toEqual([]);
    expect(useServicesStore.getState().error).toBe('Network error');
  });

  it('fetchServiceDetail success', async () => {
    const detail = { id: 's1', name: 'Svc1', version: '1.0', serviceDir: '/d', hasSchema: true, projectCount: 0, manifest: {}, configSchema: {}, configSchemaFields: [], projects: [] };
    vi.mocked(servicesApi.detail).mockResolvedValue(detail as any);

    await useServicesStore.getState().fetchServiceDetail('s1');

    expect(useServicesStore.getState().currentService).toEqual(detail);
  });

  it('createService success', async () => {
    vi.mocked(servicesApi.create).mockResolvedValue({ id: 's1', name: 's1', version: '1.0.0', created: true } as any);
    vi.mocked(servicesApi.list).mockResolvedValue({ services: [] });

    const result = await useServicesStore.getState().createService({ id: 's1', template: 'empty' });

    expect(result).toBe(true);
    expect(servicesApi.create).toHaveBeenCalled();
  });

  it('deleteService success', async () => {
    useServicesStore.setState({ services: [{ id: 's1', name: 'Svc1', version: '1.0', serviceDir: '/d', hasSchema: true, projectCount: 0 }] });
    vi.mocked(servicesApi.delete).mockResolvedValue(undefined as any);

    const result = await useServicesStore.getState().deleteService('s1');

    expect(result).toBe(true);
    expect(useServicesStore.getState().services).toEqual([]);
  });

  it('scanServices calls API then refreshes list', async () => {
    vi.mocked(servicesApi.scan).mockResolvedValue({});
    vi.mocked(servicesApi.list).mockResolvedValue({ services: [{ id: 's1', name: 'Svc1', version: '1.0', serviceDir: '/d', hasSchema: false, projectCount: 0 }] });

    await useServicesStore.getState().scanServices();

    expect(servicesApi.scan).toHaveBeenCalled();
    expect(servicesApi.list).toHaveBeenCalled();
    expect(useServicesStore.getState().services).toHaveLength(1);
  });
});
