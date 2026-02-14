import { create } from 'zustand';
import { EventStream } from '@/api/event-stream';
import type { ServerEvent } from '@/types/server';
import { useInstancesStore } from '@/stores/useInstancesStore';
import { useDashboardStore } from '@/stores/useDashboardStore';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';
import { useDriversStore } from '@/stores/useDriversStore';

export type SseStatus = 'disconnected' | 'connecting' | 'connected' | 'reconnecting' | 'error';

const MAX_RECENT_EVENTS = 50;
const INITIAL_DELAY_MS = 1000;
const MAX_DELAY_MS = 30000;
const BACKOFF_MULTIPLIER = 2;

interface EventStreamState {
  status: SseStatus;
  reconnectAttempts: number;
  lastEventTime: number | null;
  recentEvents: ServerEvent[];
  error: string | null;

  connect: () => void;
  disconnect: () => void;
  getStatus: () => SseStatus;

  // internal
  _stream: EventStream | null;
  _reconnectTimer: ReturnType<typeof setTimeout> | null;
}

export function getReconnectDelay(attempts: number): number {
  const delay = INITIAL_DELAY_MS * Math.pow(BACKOFF_MULTIPLIER, attempts);
  return Math.min(delay, MAX_DELAY_MS);
}

export function dispatchEvent(event: ServerEvent): void {
  switch (event.type) {
    case 'instance.started':
    case 'instance.finished':
      useInstancesStore.getState().fetchInstances();
      useDashboardStore.getState().fetchServerStatus();
      if (event.data.projectId) {
        useProjectsStore.getState().fetchRuntimes();
      }
      break;
    case 'schedule.triggered':
    case 'schedule.suppressed':
      useDashboardStore.getState().addEvent(event);
      break;
    case 'project.status_changed':
      // Backend pending - branch reserved
      useProjectsStore.getState().fetchProjects();
      break;
    case 'service.scanned':
      // Backend pending - branch reserved
      useServicesStore.getState().fetchServices();
      break;
    case 'driver.scanned':
      // Backend pending - branch reserved
      useDriversStore.getState().fetchDrivers();
      break;
  }
}

function onReconnected(): void {
  useDashboardStore.getState().fetchServerStatus();
  useDashboardStore.getState().fetchInstances();
  useInstancesStore.getState().fetchInstances();
  useProjectsStore.getState().fetchProjects();
}

export const useEventStreamStore = create<EventStreamState>()((set, get) => ({
  status: 'disconnected',
  reconnectAttempts: 0,
  lastEventTime: null,
  recentEvents: [],
  error: null,
  _stream: null,
  _reconnectTimer: null,

  connect: () => {
    const prev = get()._stream;
    if (prev) prev.close();
    const prevTimer = get()._reconnectTimer;
    if (prevTimer) clearTimeout(prevTimer);

    const stream = new EventStream();
    const wasReconnecting = get().status === 'reconnecting';
    set({ status: 'connecting', _stream: stream, _reconnectTimer: null, error: null });

    stream.on('connected', () => {
      const isReconnect = wasReconnecting || get().reconnectAttempts > 0;
      set({ status: 'connected', reconnectAttempts: 0, error: null });
      if (isReconnect) {
        onReconnected();
      }
    });

    stream.on('event', (event: ServerEvent) => {
      set((s) => ({
        lastEventTime: Date.now(),
        recentEvents: [event, ...s.recentEvents].slice(0, MAX_RECENT_EVENTS),
      }));
      dispatchEvent(event);
    });

    stream.on('error', () => {
      const state = get();
      if (state.status === 'disconnected') return; // intentional disconnect
      const attempts = state.reconnectAttempts;
      set({ status: 'reconnecting', reconnectAttempts: attempts + 1, error: 'Connection lost' });

      // Close current stream
      state._stream?.close();

      const delay = getReconnectDelay(attempts);
      const timer = setTimeout(() => {
        get().connect();
      }, delay);
      set({ _reconnectTimer: timer, _stream: null });
    });

    stream.connect();
  },

  disconnect: () => {
    const state = get();
    if (state._stream) state._stream.close();
    if (state._reconnectTimer) clearTimeout(state._reconnectTimer);
    set({
      status: 'disconnected',
      _stream: null,
      _reconnectTimer: null,
      error: null,
    });
  },

  getStatus: () => get().status,
}));
