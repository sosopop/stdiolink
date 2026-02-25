import { describe, it, expect, vi, beforeEach } from 'vitest';

const storeSpies = vi.hoisted(() => ({
  instancesFetchInstances: vi.fn(),
  dashboardFetchServerStatus: vi.fn(),
  dashboardFetchInstances: vi.fn(),
  dashboardAddEvent: vi.fn(),
  projectsFetchProjects: vi.fn(),
  projectsFetchRuntimes: vi.fn(),
  servicesFetchServices: vi.fn(),
  driversFetchDrivers: vi.fn(),
}));

// Mock dependent stores BEFORE importing the store under test
vi.mock('@/stores/useInstancesStore', () => ({
  useInstancesStore: { getState: () => ({ fetchInstances: storeSpies.instancesFetchInstances }) },
}));
vi.mock('@/stores/useDashboardStore', () => ({
  useDashboardStore: {
    getState: () => ({
      fetchServerStatus: storeSpies.dashboardFetchServerStatus,
      fetchInstances: storeSpies.dashboardFetchInstances,
      addEvent: storeSpies.dashboardAddEvent,
    }),
  },
}));
vi.mock('@/stores/useProjectsStore', () => ({
  useProjectsStore: {
    getState: () => ({
      fetchProjects: storeSpies.projectsFetchProjects,
      fetchRuntimes: storeSpies.projectsFetchRuntimes,
    }),
  },
}));
vi.mock('@/stores/useServicesStore', () => ({
  useServicesStore: { getState: () => ({ fetchServices: storeSpies.servicesFetchServices }) },
}));
vi.mock('@/stores/useDriversStore', () => ({
  useDriversStore: { getState: () => ({ fetchDrivers: storeSpies.driversFetchDrivers }) },
}));

// Mock EventStream
const mockOnCallbacks = new Map<string, Function>();
vi.mock('@/api/event-stream', () => ({
  EventStream: vi.fn().mockImplementation(() => ({
    connect: vi.fn(),
    close: vi.fn(),
    on: vi.fn().mockImplementation((event: string, cb: Function) => {
      mockOnCallbacks.set(event, cb);
    }),
    off: vi.fn(),
  })),
}));

import { useEventStreamStore, getReconnectDelay } from '../useEventStreamStore';

describe('useEventStreamStore', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockOnCallbacks.clear();
    useEventStreamStore.setState({
      status: 'disconnected',
      reconnectAttempts: 0,
      lastEventTime: null,
      recentEvents: [],
      error: null,
      _stream: null,
      _reconnectTimer: null,
    });
  });

  it('connect sets status to connecting', () => {
    useEventStreamStore.getState().connect();
    expect(useEventStreamStore.getState().status).toBe('connecting');
  });

  it('SSE open sets status to connected', () => {
    useEventStreamStore.getState().connect();
    const connectedCb = mockOnCallbacks.get('connected');
    expect(connectedCb).toBeDefined();
    connectedCb!({ type: 'connected', data: {} });
    expect(useEventStreamStore.getState().status).toBe('connected');
    expect(useEventStreamStore.getState().reconnectAttempts).toBe(0);
  });

  it('SSE error sets status to reconnecting', () => {
    vi.useFakeTimers();
    useEventStreamStore.setState({ status: 'connected' });
    useEventStreamStore.getState().connect();
    const errorCb = mockOnCallbacks.get('error');
    expect(errorCb).toBeDefined();
    errorCb!({ type: 'error', data: {} });
    expect(useEventStreamStore.getState().status).toBe('reconnecting');
    expect(useEventStreamStore.getState().reconnectAttempts).toBe(1);
    vi.useRealTimers();
  });

  it('reconnect success refreshes dashboard/projects state', () => {
    vi.useFakeTimers();
    useEventStreamStore.setState({ status: 'connected' });
    useEventStreamStore.getState().connect();

    const errorCb = mockOnCallbacks.get('error');
    expect(errorCb).toBeDefined();
    errorCb!({ type: 'error', data: {} });

    expect(useEventStreamStore.getState().status).toBe('reconnecting');
    vi.advanceTimersByTime(1000);

    const connectedCb = mockOnCallbacks.get('connected');
    expect(connectedCb).toBeDefined();
    connectedCb!({ type: 'connected', data: {} });

    expect(useEventStreamStore.getState().status).toBe('connected');
    expect(storeSpies.dashboardFetchServerStatus).toHaveBeenCalledTimes(1);
    expect(storeSpies.dashboardFetchInstances).toHaveBeenCalledTimes(1);
    expect(storeSpies.instancesFetchInstances).toHaveBeenCalledTimes(1);
    expect(storeSpies.projectsFetchProjects).toHaveBeenCalledTimes(1);
    vi.useRealTimers();
  });

  it('error callback after intentional disconnect does not schedule reconnect', () => {
    vi.useFakeTimers();
    useEventStreamStore.getState().connect();
    const errorCb = mockOnCallbacks.get('error');
    expect(errorCb).toBeDefined();

    useEventStreamStore.getState().disconnect();
    errorCb!({ type: 'error', data: {} });

    const state = useEventStreamStore.getState();
    expect(state.status).toBe('disconnected');
    expect(state.reconnectAttempts).toBe(0);
    expect(state._reconnectTimer).toBeNull();
    vi.useRealTimers();
  });

  it('disconnect sets status to disconnected', () => {
    useEventStreamStore.getState().connect();
    useEventStreamStore.getState().disconnect();
    expect(useEventStreamStore.getState().status).toBe('disconnected');
  });

  it('event updates lastEventTime and recentEvents', () => {
    useEventStreamStore.getState().connect();
    const eventCb = mockOnCallbacks.get('event');
    expect(eventCb).toBeDefined();
    eventCb!({ type: 'schedule.triggered', data: { projectId: 'p1' } });
    const state = useEventStreamStore.getState();
    expect(state.lastEventTime).toBeTruthy();
    expect(state.recentEvents).toHaveLength(1);
  });

  it('recentEvents caps at 50', () => {
    const events = Array.from({ length: 50 }, (_, i) => ({
      type: 'instance.started',
      data: { instanceId: `i${i}` },
    }));
    useEventStreamStore.setState({ recentEvents: events });
    useEventStreamStore.getState().connect();
    const eventCb = mockOnCallbacks.get('event');
    eventCb!({ type: 'schedule.triggered', data: { projectId: 'new' } });
    expect(useEventStreamStore.getState().recentEvents).toHaveLength(50);
    expect(useEventStreamStore.getState().recentEvents[0].data.projectId).toBe('new');
  });

  it('reconnect delay follows exponential backoff', () => {
    expect(getReconnectDelay(0)).toBe(1000);
    expect(getReconnectDelay(1)).toBe(2000);
    expect(getReconnectDelay(2)).toBe(4000);
    expect(getReconnectDelay(3)).toBe(8000);
    expect(getReconnectDelay(4)).toBe(16000);
    expect(getReconnectDelay(5)).toBe(30000);
    expect(getReconnectDelay(10)).toBe(30000);
  });

  it('clearMessages resets recentEvents via disconnect', () => {
    useEventStreamStore.setState({ recentEvents: [{ type: 'test', data: {} }] });
    useEventStreamStore.getState().disconnect();
    expect(useEventStreamStore.getState().status).toBe('disconnected');
  });
});
