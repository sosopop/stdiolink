export type WsMessageType =
  | 'driver.started'
  | 'driver.restarted'
  | 'meta'
  | 'stdout'
  | 'driver.exited'
  | 'error';

export interface WsMessage {
  type: WsMessageType;
  [key: string]: unknown;
}

export class DriverLabWsClient {
  private ws: WebSocket | null = null;
  private listeners = new Map<string, Set<(data: unknown) => void>>();

  connect(driverId: string, runMode: 'oneshot' | 'keepalive', args: string[] = []): void {
    const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const host = window.location.host;
    const params = new URLSearchParams({ runMode });
    if (args.length > 0) params.set('args', args.join(','));

    this.ws = new WebSocket(
      `${proto}://${host}/api/driverlab/${encodeURIComponent(driverId)}?${params}`,
    );

    this.ws.onopen = () => this.emit('connected', {});
    this.ws.onmessage = (e) => {
      try {
        const msg: WsMessage = JSON.parse(e.data as string);
        this.emit('message', msg);
        this.emit(msg.type, msg);
      } catch {
        /* ignore non-JSON */
      }
    };
    this.ws.onerror = (e) => this.emit('error', e);
    this.ws.onclose = () => this.emit('disconnected', {});
  }

  send(message: Record<string, unknown>): void {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(message));
    }
  }

  exec(command: string, data: Record<string, unknown>): void {
    this.send({ type: 'exec', cmd: command, data });
  }

  cancel(): void {
    this.send({ type: 'cancel' });
  }

  disconnect(): void {
    this.ws?.close();
    this.ws = null;
  }

  on(event: string, callback: (data: unknown) => void): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(callback);
  }

  off(event: string, callback: (data: unknown) => void): void {
    this.listeners.get(event)?.delete(callback);
  }

  private emit(event: string, data: unknown): void {
    this.listeners.get(event)?.forEach((cb) => cb(data));
  }
}
