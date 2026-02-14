import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { EventStream } from '../event-stream';

class MockEventSource {
  static instances: MockEventSource[] = [];

  url: string;
  onopen: ((ev: Event) => void) | null = null;
  onerror: ((ev: Event) => void) | null = null;
  private eventListeners = new Map<string, ((e: Event) => void)[]>();

  constructor(url: string) {
    this.url = url;
    MockEventSource.instances.push(this);
  }

  addEventListener(type: string, listener: (e: Event) => void) {
    if (!this.eventListeners.has(type)) this.eventListeners.set(type, []);
    this.eventListeners.get(type)!.push(listener);
  }

  close() {
    /* noop */
  }

  simulateOpen() {
    this.onopen?.({} as Event);
  }

  simulateError() {
    this.onerror?.({} as Event);
  }

  simulateEvent(type: string, data: unknown) {
    const listeners = this.eventListeners.get(type) || [];
    const event = { data: JSON.stringify(data) } as unknown as Event;
    listeners.forEach((l) => l(event));
  }
}

function lastEs(): MockEventSource {
  return MockEventSource.instances[MockEventSource.instances.length - 1]!;
}

describe('EventStream', () => {
  let stream: EventStream;
  const originalEventSource = globalThis.EventSource;

  beforeEach(() => {
    MockEventSource.instances = [];
    (globalThis as any).EventSource = MockEventSource as any;
    stream = new EventStream();
  });

  afterEach(() => {
    (globalThis as any).EventSource = originalEventSource;
  });

  it('connect() creates EventSource with correct URL', () => {
    stream.connect();
    expect(MockEventSource.instances).toHaveLength(1);
    expect(lastEs().url).toBe('/api/events/stream?');
  });

  it('connect() passes filter params', () => {
    stream.connect(['instance.started', 'instance.finished']);
    expect(lastEs().url).toBe(
      '/api/events/stream?filter=instance.started%2Cinstance.finished',
    );
  });

  it('emits connected event on open', () => {
    const cb = vi.fn();
    stream.on('connected', cb);
    stream.connect();
    lastEs().simulateOpen();
    expect(cb).toHaveBeenCalledWith({ type: 'connected', data: {} });
  });

  it('emits error event on error', () => {
    const cb = vi.fn();
    stream.on('error', cb);
    stream.connect();
    lastEs().simulateError();
    expect(cb).toHaveBeenCalledWith({ type: 'error', data: {} });
  });

  it('emits event and typed event on SSE message', () => {
    const eventCb = vi.fn();
    const typedCb = vi.fn();
    stream.on('event', eventCb);
    stream.on('instance.started', typedCb);
    stream.connect();
    lastEs().simulateEvent('instance.started', { instanceId: 'i1' });
    expect(eventCb).toHaveBeenCalledWith({
      type: 'instance.started',
      data: { instanceId: 'i1' },
    });
    expect(typedCb).toHaveBeenCalledWith({
      type: 'instance.started',
      data: { instanceId: 'i1' },
    });
  });

  it('handles all registered event types', () => {
    stream.connect();
    const es = lastEs();
    const types = [
      'instance.started',
      'instance.finished',
      'schedule.triggered',
      'schedule.suppressed',
    ];
    for (const type of types) {
      const cb = vi.fn();
      stream.on(type, cb);
      es.simulateEvent(type, { id: '1' });
      expect(cb).toHaveBeenCalled();
    }
  });

  it('close() closes EventSource', () => {
    stream.connect();
    const es = lastEs();
    const closeSpy = vi.spyOn(es, 'close');
    stream.close();
    expect(closeSpy).toHaveBeenCalled();
  });

  it('off() removes listener', () => {
    const cb = vi.fn();
    stream.on('connected', cb);
    stream.off('connected', cb);
    stream.connect();
    lastEs().simulateOpen();
    expect(cb).not.toHaveBeenCalled();
  });
});
