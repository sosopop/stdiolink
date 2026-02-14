import type { ServerEvent } from '@/types/server';

export class EventStream {
  private es: EventSource | null = null;
  private listeners = new Map<string, Set<(event: ServerEvent) => void>>();

  connect(filters: string[] = []): void {
    const params = new URLSearchParams();
    if (filters.length > 0) params.set('filter', filters.join(','));

    this.es = new EventSource(`/api/events/stream?${params}`);
    this.es.onopen = () => this.emit('connected', { type: 'connected', data: {} });
    this.es.onerror = () => this.emit('error', { type: 'error', data: {} });

    const eventTypes = [
      'instance.started',
      'instance.finished',
      'schedule.triggered',
      'schedule.suppressed',
    ];

    for (const type of eventTypes) {
      this.es.addEventListener(type, (e: Event) => {
        const me = e as MessageEvent;
        const event: ServerEvent = { type, data: JSON.parse(me.data) };
        this.emit('event', event);
        this.emit(type, event);
      });
    }
  }

  close(): void {
    this.es?.close();
    this.es = null;
  }

  on(event: string, callback: (event: ServerEvent) => void): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(callback);
  }

  off(event: string, callback: (event: ServerEvent) => void): void {
    this.listeners.get(event)?.delete(callback);
  }

  private emit(event: string, data: ServerEvent): void {
    this.listeners.get(event)?.forEach((cb) => cb(data));
  }
}
