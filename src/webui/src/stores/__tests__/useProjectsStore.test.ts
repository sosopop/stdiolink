import { describe, it, expect, vi, beforeEach } from 'vitest';
import { projectsApi } from '@/api/projects';
import { useProjectsStore } from '../useProjectsStore';

vi.mock('@/api/projects', () => ({
  projectsApi: {
    list: vi.fn(),
    detail: vi.fn(),
    runtimeBatch: vi.fn(),
    runtime: vi.fn(),
    create: vi.fn(),
    update: vi.fn(),
    delete: vi.fn(),
    start: vi.fn(),
    stop: vi.fn(),
    reload: vi.fn(),
    setEnabled: vi.fn(),
  },
}));

describe('useProjectsStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useProjectsStore.setState({
      projects: [],
      runtimes: {},
      currentProject: null,
      currentRuntime: null,
      loading: false,
      error: null,
    });
  });

  it('fetchProjects success', async () => {
    const mockProjects = [{ id: 'p1', name: 'P1', serviceId: 's1', enabled: true, valid: true, config: {}, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' }];
    vi.mocked(projectsApi.list).mockResolvedValue({ projects: mockProjects, total: 1, page: 1, pageSize: 20 });

    await useProjectsStore.getState().fetchProjects();

    expect(useProjectsStore.getState().projects).toEqual(mockProjects);
    expect(useProjectsStore.getState().loading).toBe(false);
  });

  it('fetchRuntimes success', async () => {
    const runtimes = [{ id: 'p1', enabled: true, valid: true, status: 'running', runningInstances: 1, instances: [], schedule: { type: 'manual', timerActive: false, restartSuppressed: false, consecutiveFailures: 0, shuttingDown: false, autoRestarting: false } }];
    vi.mocked(projectsApi.runtimeBatch).mockResolvedValue({ runtimes });

    await useProjectsStore.getState().fetchRuntimes();

    expect(useProjectsStore.getState().runtimes).toEqual({ p1: runtimes[0] });
  });

  it('createProject success', async () => {
    vi.mocked(projectsApi.create).mockResolvedValue({ id: 'p1', name: 'P1' } as any);
    vi.mocked(projectsApi.list).mockResolvedValue({ projects: [], total: 0, page: 1, pageSize: 20 });

    const result = await useProjectsStore.getState().createProject({ id: 'p1', name: 'P1', serviceId: 's1' });

    expect(result).toBe(true);
    expect(projectsApi.create).toHaveBeenCalled();
  });

  it('updateProject success', async () => {
    const updated = { id: 'p1', name: 'Updated', serviceId: 's1', enabled: true, valid: true, config: { host: 'new' }, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' };
    vi.mocked(projectsApi.update).mockResolvedValue(updated as any);

    const result = await useProjectsStore.getState().updateProject('p1', { name: 'Updated' });

    expect(result).toBe(true);
    expect(useProjectsStore.getState().currentProject).toEqual(updated);
  });

  it('deleteProject success', async () => {
    useProjectsStore.setState({ projects: [{ id: 'p1', name: 'P1', serviceId: 's1', enabled: true, valid: true, config: {}, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' }] as any });
    vi.mocked(projectsApi.delete).mockResolvedValue(undefined as any);

    const result = await useProjectsStore.getState().deleteProject('p1');

    expect(result).toBe(true);
    expect(useProjectsStore.getState().projects).toEqual([]);
  });

  it('startProject success', async () => {
    vi.mocked(projectsApi.start).mockResolvedValue({ instanceId: 'i1', pid: 1234 });
    vi.mocked(projectsApi.runtime).mockResolvedValue({ id: 'p1', status: 'running', runningInstances: 1 } as any);

    const result = await useProjectsStore.getState().startProject('p1');

    expect(result).toBe(true);
    expect(projectsApi.start).toHaveBeenCalledWith('p1');
  });

  it('stopProject success', async () => {
    vi.mocked(projectsApi.stop).mockResolvedValue({ stopped: true });
    vi.mocked(projectsApi.runtime).mockResolvedValue({ id: 'p1', status: 'stopped', runningInstances: 0 } as any);

    const result = await useProjectsStore.getState().stopProject('p1');

    expect(result).toBe(true);
  });

  it('setEnabled success', async () => {
    useProjectsStore.setState({ projects: [{ id: 'p1', name: 'P1', serviceId: 's1', enabled: false, valid: true, config: {}, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' }] as any });
    const updated = { id: 'p1', name: 'P1', serviceId: 's1', enabled: true, valid: true, config: {}, schedule: { type: 'manual' }, instanceCount: 0, status: 'stopped' };
    vi.mocked(projectsApi.setEnabled).mockResolvedValue(updated as any);

    const result = await useProjectsStore.getState().setEnabled('p1', true);

    expect(result).toBe(true);
    expect(useProjectsStore.getState().projects[0].enabled).toBe(true);
  });
});
