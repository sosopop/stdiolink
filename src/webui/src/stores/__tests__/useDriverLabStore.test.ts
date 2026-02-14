import { describe, it, expect, vi, beforeEach } from 'vitest';
import { useDriverLabStore } from '../useDriverLabStore';
import type { WsMessage } from '@/api/driverlab-ws';

// Mock the DriverLabWsClient
vi.mock('@/api/driverlab-ws', () => {
  const listeners = new Map<string, Set<(data: unknown) => void>>();
  return {
    DriverLabWsClient: vi.fn().mockImplementation(() => ({
      connect: vi.fn().mockImplementation(() => {
        // Simulate connected event
        setTimeout(() => {
          listeners.get('connected')?.forEach((cb) => cb({}));
        }, 0);
      }),
      disconnect: vi.fn(),
      exec: vi.fn(),
      cancel: vi.fn(),
      send: vi.fn(),
      on: vi.fn().mockImplementation((event: string, cb: (data: unknown) => void) => {
        if (!listeners.has(event)) listeners.set(event, new Set());
        listeners.get(event)!.add(cb);
      }),
      off: vi.fn(),
      _listeners: listeners,
    })),
  };
});

describe('useDriverLabStore', () => {
  beforeEach(() => {
    useDriverLabStore.setState({
      connection: {
        status: 'disconnected',
        driverId: null,
        runMode: 'oneshot',
        pid: null,
        connectedAt: null,
        meta: null,
        error: null,
      },
      messages: [],
      commands: [],
      selectedCommand: null,
      commandParams: {},
      executing: false,
      autoScroll: true,
      _wsClient: null,
    });
  });

  it('connect sets status to connecting', () => {
    const { connect } = useDriverLabStore.getState();
    connect('drv1', 'oneshot');
    const state = useDriverLabStore.getState();
    expect(state.connection.status).toBe('connecting');
    expect(state.connection.driverId).toBe('drv1');
    expect(state.connection.runMode).toBe('oneshot');
  });

  it('handleWsMessage driver.started updates pid', () => {
    const { handleWsMessage } = useDriverLabStore.getState();
    handleWsMessage({ type: 'driver.started', pid: 1234 } as WsMessage);
    const state = useDriverLabStore.getState();
    expect(state.connection.pid).toBe(1234);
    expect(state.messages).toHaveLength(1);
    expect(state.messages[0].type).toBe('driver.started');
  });

  it('handleWsMessage meta updates commands', () => {
    const meta = {
      schemaVersion: '1',
      info: { name: 'Test', version: '1.0' },
      commands: [{ name: 'add', params: [], returns: { type: 'double' } }],
    };
    const { handleWsMessage } = useDriverLabStore.getState();
    handleWsMessage({ type: 'meta', meta } as unknown as WsMessage);
    const state = useDriverLabStore.getState();
    expect(state.commands).toHaveLength(1);
    expect(state.commands[0].name).toBe('add');
    expect(state.connection.meta).toBeTruthy();
  });

  it('handleWsMessage stdout appends message', () => {
    useDriverLabStore.setState({ executing: true });
    const { handleWsMessage } = useDriverLabStore.getState();
    handleWsMessage({ type: 'stdout', message: { status: 'done', code: 0, data: { ok: 42 } } } as unknown as WsMessage);
    const state = useDriverLabStore.getState();
    expect(state.messages).toHaveLength(1);
    expect(state.messages[0].payload).toEqual({ status: 'done', code: 0, data: { ok: 42 } });
    expect(state.executing).toBe(false);
  });

  it('handleWsMessage driver.exited in keepalive disconnects', () => {
    useDriverLabStore.setState({
      connection: {
        status: 'connected',
        driverId: 'drv1',
        runMode: 'keepalive',
        pid: 1234,
        connectedAt: Date.now(),
        meta: null,
        error: null,
      },
    });
    const { handleWsMessage } = useDriverLabStore.getState();
    handleWsMessage({ type: 'driver.exited', exitCode: 0, exitStatus: 'normal', reason: '' } as unknown as WsMessage);
    expect(useDriverLabStore.getState().connection.status).toBe('disconnected');
  });

  it('handleWsMessage error updates connection error', () => {
    const { handleWsMessage } = useDriverLabStore.getState();
    handleWsMessage({ type: 'error', message: 'Something failed' } as unknown as WsMessage);
    expect(useDriverLabStore.getState().connection.error).toBe('Something failed');
  });

  it('execCommand sends exec and sets executing', () => {
    useDriverLabStore.setState({ _wsClient: { exec: vi.fn(), cancel: vi.fn(), disconnect: vi.fn() } as any });
    const { execCommand } = useDriverLabStore.getState();
    execCommand('add', { a: 1, b: 2 });
    const state = useDriverLabStore.getState();
    expect(state.executing).toBe(true);
    expect(state.messages).toHaveLength(1);
    expect(state.messages[0].direction).toBe('send');
    expect(state.messages[0].type).toBe('exec');
  });

  it('cancelCommand sends cancel', () => {
    const cancelFn = vi.fn();
    useDriverLabStore.setState({ _wsClient: { exec: vi.fn(), cancel: cancelFn, disconnect: vi.fn() } as any, executing: true });
    const { cancelCommand } = useDriverLabStore.getState();
    cancelCommand();
    expect(cancelFn).toHaveBeenCalled();
    expect(useDriverLabStore.getState().executing).toBe(false);
  });

  it('disconnect closes connection', () => {
    const disconnectFn = vi.fn();
    useDriverLabStore.setState({
      _wsClient: { disconnect: disconnectFn } as any,
      connection: { status: 'connected', driverId: 'drv1', runMode: 'oneshot', pid: null, connectedAt: null, meta: null, error: null },
    });
    const { disconnect } = useDriverLabStore.getState();
    disconnect();
    expect(disconnectFn).toHaveBeenCalled();
    expect(useDriverLabStore.getState().connection.status).toBe('disconnected');
  });

  it('clearMessages empties the list', () => {
    useDriverLabStore.setState({
      messages: [{ id: '1', timestamp: 0, direction: 'recv', type: 'stdout', raw: {} as any, payload: {}, expanded: false }],
    });
    useDriverLabStore.getState().clearMessages();
    expect(useDriverLabStore.getState().messages).toHaveLength(0);
  });

  it('appendMessage caps at 500', () => {
    const msgs = Array.from({ length: 500 }, (_, i) => ({
      id: `m-${i}`,
      timestamp: i,
      direction: 'recv' as const,
      type: 'stdout',
      raw: {} as any,
      payload: i,
      expanded: false,
    }));
    useDriverLabStore.setState({ messages: msgs });
    useDriverLabStore.getState().appendMessage({
      id: 'new',
      timestamp: 999,
      direction: 'recv',
      type: 'stdout',
      raw: {} as any,
      payload: 'new',
      expanded: false,
    });
    const state = useDriverLabStore.getState();
    expect(state.messages).toHaveLength(500);
    expect(state.messages[state.messages.length - 1].id).toBe('new');
    expect(state.messages[0].id).toBe('m-1');
  });

  it('selectCommand updates selection and resets params', () => {
    useDriverLabStore.setState({ commandParams: { a: 1 } });
    useDriverLabStore.getState().selectCommand('ping');
    const state = useDriverLabStore.getState();
    expect(state.selectedCommand).toBe('ping');
    expect(state.commandParams).toEqual({});
  });
});
