import { describe, it, expect, vi, beforeEach } from 'vitest';
import { instancesApi } from '@/api/instances';
import { useInstancesStore } from '../useInstancesStore';

vi.mock('@/api/instances', () => ({
  instancesApi: {
    list: vi.fn(),
    detail: vi.fn(),
    processTree: vi.fn(),
    resources: vi.fn(),
    logs: vi.fn(),
    terminate: vi.fn(),
  },
}));

const mockInstance = {
  id: 'inst-1',
  projectId: 'p1',
  serviceId: 's1',
  pid: 1234,
  startedAt: '2025-01-01T00:00:00Z',
  status: 'running',
};

describe('useInstancesStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useInstancesStore.setState({
      instances: [],
      currentInstance: null,
      processTree: null,
      resources: [],
      resourceHistory: [],
      logs: [],
      loading: false,
      error: null,
    });
  });

  it('fetchInstances updates list', async () => {
    vi.mocked(instancesApi.list).mockResolvedValue({ instances: [mockInstance] });
    await useInstancesStore.getState().fetchInstances();
    expect(useInstancesStore.getState().instances).toHaveLength(1);
    expect(useInstancesStore.getState().instances[0].id).toBe('inst-1');
  });

  it('fetchInstanceDetail updates currentInstance', async () => {
    vi.mocked(instancesApi.detail).mockResolvedValue(mockInstance);
    await useInstancesStore.getState().fetchInstanceDetail('inst-1');
    expect(useInstancesStore.getState().currentInstance?.id).toBe('inst-1');
  });

  it('fetchProcessTree updates processTree', async () => {
    const tree = { pid: 1234, name: 'node', commandLine: 'node', status: 'running', resources: { cpuPercent: 10, memoryRssBytes: 1024, threadCount: 2 }, children: [] };
    const summary = { totalProcesses: 1, totalCpuPercent: 10, totalMemoryRssBytes: 1024, totalThreads: 2 };
    vi.mocked(instancesApi.processTree).mockResolvedValue({ instanceId: 'inst-1', rootPid: 1234, tree, summary });
    await useInstancesStore.getState().fetchProcessTree('inst-1');
    expect(useInstancesStore.getState().processTree).not.toBeNull();
    expect(useInstancesStore.getState().processTree?.summary.totalProcesses).toBe(1);
  });

  it('fetchResources updates resources and appends sample', async () => {
    const processes = [{ pid: 1234, name: 'node', cpuPercent: 25, memoryRssBytes: 2048, threadCount: 4, uptimeSeconds: 100, ioReadBytes: 0, ioWriteBytes: 0 }];
    const summary = { totalProcesses: 1, totalCpuPercent: 25, totalMemoryRssBytes: 2048, totalThreads: 4 };
    vi.mocked(instancesApi.resources).mockResolvedValue({ instanceId: 'inst-1', timestamp: '2025-01-01T00:00:00Z', processes, summary });
    await useInstancesStore.getState().fetchResources('inst-1');
    expect(useInstancesStore.getState().resources).toHaveLength(1);
    expect(useInstancesStore.getState().resourceHistory).toHaveLength(1);
  });

  it('fetchLogs updates logs', async () => {
    vi.mocked(instancesApi.logs).mockResolvedValue({ projectId: 'p1', lines: ['[INFO] started', '[ERROR] fail'] });
    await useInstancesStore.getState().fetchLogs('inst-1');
    expect(useInstancesStore.getState().logs).toHaveLength(2);
  });

  it('terminateInstance removes from list', async () => {
    useInstancesStore.setState({ instances: [mockInstance] });
    vi.mocked(instancesApi.terminate).mockResolvedValue({ terminated: true });
    await useInstancesStore.getState().terminateInstance('inst-1');
    expect(useInstancesStore.getState().instances).toHaveLength(0);
  });

  it('appendResourceSample caps at 60', () => {
    const samples = Array.from({ length: 65 }, (_, i) => ({
      timestamp: i,
      cpuPercent: i,
      memoryRssBytes: i * 1024,
      threadCount: 1,
    }));
    for (const s of samples) {
      useInstancesStore.getState().appendResourceSample(s);
    }
    expect(useInstancesStore.getState().resourceHistory).toHaveLength(60);
    expect(useInstancesStore.getState().resourceHistory[0].timestamp).toBe(5);
  });
});
