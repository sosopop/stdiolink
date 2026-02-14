import { describe, it, expect, vi, beforeEach } from 'vitest';
import { useDashboardStore } from '@/stores/useDashboardStore';
import { serverApi } from '@/api/server';
import { instancesApi } from '@/api/instances';

vi.mock('@/api/server', () => ({
  serverApi: { status: vi.fn() },
}));

vi.mock('@/api/instances', () => ({
  instancesApi: { list: vi.fn() },
}));

describe('useDashboardStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    useDashboardStore.setState({
      serverStatus: null,
      instances: [],
      events: [],
      loading: false,
      error: null,
      connected: false,
    });
  });

  it('fetchServerStatus success updates serverStatus', async () => {
    const mockStatus = { status: 'running', version: '1.0', uptimeMs: 60000 };
    vi.mocked(serverApi.status).mockResolvedValue(mockStatus as any);

    await useDashboardStore.getState().fetchServerStatus();

    const state = useDashboardStore.getState();
    expect(state.serverStatus).toEqual(mockStatus);
    expect(state.loading).toBe(false);
    expect(state.error).toBeNull();
  });

  it('fetchServerStatus failure sets error', async () => {
    vi.mocked(serverApi.status).mockRejectedValue({ error: 'Network error' });

    await useDashboardStore.getState().fetchServerStatus();

    const state = useDashboardStore.getState();
    expect(state.error).toBe('Network error');
    expect(state.loading).toBe(false);
  });

  it('fetchInstances success updates instances', async () => {
    const mockInstances = [{ id: 'i1', projectId: 'p1', status: 'running' }];
    vi.mocked(instancesApi.list).mockResolvedValue({ instances: mockInstances } as any);

    await useDashboardStore.getState().fetchInstances();

    expect(useDashboardStore.getState().instances).toEqual(mockInstances);
  });

  it('addEvent prepends event to list', () => {
    const event1 = { type: 'instance.started', data: { id: '1' } };
    const event2 = { type: 'instance.finished', data: { id: '2' } };

    useDashboardStore.getState().addEvent(event1);
    useDashboardStore.getState().addEvent(event2);

    const events = useDashboardStore.getState().events;
    expect(events).toHaveLength(2);
    expect(events[0]).toEqual(event2);
    expect(events[1]).toEqual(event1);
  });

  it('addEvent caps at 50 events', () => {
    for (let i = 0; i < 55; i++) {
      useDashboardStore.getState().addEvent({ type: 'test', data: { i } });
    }
    expect(useDashboardStore.getState().events).toHaveLength(50);
  });

  it('setConnected updates connected state', () => {
    useDashboardStore.getState().setConnected(true);
    expect(useDashboardStore.getState().connected).toBe(true);
    useDashboardStore.getState().setConnected(false);
    expect(useDashboardStore.getState().connected).toBe(false);
  });
});
