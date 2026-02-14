import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { DriverLabWsClient } from '../driverlab-ws';

class MockWebSocket {
  static OPEN = 1;
  static CLOSED = 3;
  static instances: MockWebSocket[] = [];

  url: string;
  readyState = MockWebSocket.OPEN;
  onopen: ((ev: Event) => void) | null = null;
  onmessage: ((ev: MessageEvent) => void) | null = null;
  onerror: ((ev: Event) => void) | null = null;
  onclose: ((ev: CloseEvent) => void) | null = null;
  sent: string[] = [];

  constructor(url: string) {
    this.url = url;
    MockWebSocket.instances.push(this);
  }

  send(data: string) {
    this.sent.push(data);
  }

  close() {
    this.readyState = MockWebSocket.CLOSED;
    this.onclose?.({} as CloseEvent);
  }

  simulateOpen() {
    this.onopen?.({} as Event);
  }

  simulateMessage(data: unknown) {
    this.onmessage?.({ data: JSON.stringify(data) } as MessageEvent);
  }

  simulateError() {
    this.onerror?.({} as Event);
  }
}

function lastWs(): MockWebSocket {
  return MockWebSocket.instances[MockWebSocket.instances.length - 1]!;
}

describe('DriverLabWsClient', () => {
  let client: DriverLabWsClient;
  const originalWebSocket = globalThis.WebSocket;

  beforeEach(() => {
    MockWebSocket.instances = [];
    (globalThis as any).WebSocket = MockWebSocket as any;
    Object.defineProperty(globalThis, 'window', {
      value: { location: { protocol: 'http:', host: 'localhost:3000' } },
      writable: true,
      configurable: true,
    });
    client = new DriverLabWsClient();
  });

  afterEach(() => {
    (globalThis as any).WebSocket = originalWebSocket;
  });

  it('connect() creates WebSocket with correct URL', () => {
    client.connect('my-driver', 'keepalive');
    expect(MockWebSocket.instances).toHaveLength(1);
    expect(lastWs().url).toContain('ws://localhost:3000/api/driverlab/my-driver');
    expect(lastWs().url).toContain('runMode=keepalive');
  });

  it('connect() encodes driver ID in URL', () => {
    client.connect('driver/special', 'oneshot');
    expect(lastWs().url).toContain('driver%2Fspecial');
  });

  it('connect() includes args in URL params', () => {
    client.connect('d1', 'oneshot', ['--port', '8080']);
    expect(lastWs().url).toContain('args=--port%2C8080');
  });

  it('emits connected event on open', () => {
    const cb = vi.fn();
    client.on('connected', cb);
    client.connect('d1', 'keepalive');
    lastWs().simulateOpen();
    expect(cb).toHaveBeenCalledWith({});
  });

  it('emits message and typed events on message', () => {
    const msgCb = vi.fn();
    const typedCb = vi.fn();
    client.on('message', msgCb);
    client.on('stdout', typedCb);
    client.connect('d1', 'keepalive');
    lastWs().simulateMessage({ type: 'stdout', line: 'hello' });
    expect(msgCb).toHaveBeenCalledWith({ type: 'stdout', line: 'hello' });
    expect(typedCb).toHaveBeenCalledWith({ type: 'stdout', line: 'hello' });
  });

  it('emits error event on error', () => {
    const cb = vi.fn();
    client.on('error', cb);
    client.connect('d1', 'keepalive');
    lastWs().simulateError();
    expect(cb).toHaveBeenCalled();
  });

  it('emits disconnected event on close', () => {
    const cb = vi.fn();
    client.on('disconnected', cb);
    client.connect('d1', 'keepalive');
    lastWs().close();
    expect(cb).toHaveBeenCalledWith({});
  });

  it('send() sends JSON string', () => {
    client.connect('d1', 'keepalive');
    client.send({ type: 'ping' });
    expect(lastWs().sent).toEqual(['{"type":"ping"}']);
  });

  it('exec() sends exec command', () => {
    client.connect('d1', 'keepalive');
    client.exec('getData', { key: 'value' });
    expect(lastWs().sent).toEqual([
      '{"type":"exec","cmd":"getData","data":{"key":"value"}}',
    ]);
  });

  it('cancel() sends cancel message', () => {
    client.connect('d1', 'keepalive');
    client.cancel();
    expect(lastWs().sent).toEqual(['{"type":"cancel"}']);
  });

  it('disconnect() closes WebSocket', () => {
    client.connect('d1', 'keepalive');
    const ws = lastWs();
    client.disconnect();
    expect(ws.readyState).toBe(MockWebSocket.CLOSED);
  });

  it('off() removes listener', () => {
    const cb = vi.fn();
    client.on('connected', cb);
    client.off('connected', cb);
    client.connect('d1', 'keepalive');
    lastWs().simulateOpen();
    expect(cb).not.toHaveBeenCalled();
  });

  it('send() does nothing when WebSocket is not open', () => {
    client.connect('d1', 'keepalive');
    lastWs().readyState = MockWebSocket.CLOSED;
    client.send({ type: 'test' });
    expect(lastWs().sent).toEqual([]);
  });
});
