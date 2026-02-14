import { create } from 'zustand';
import type { ServerStatus, ServerEvent } from '@/types/server';
import type { Instance } from '@/types/instance';
import { serverApi } from '@/api/server';
import { instancesApi } from '@/api/instances';

const MAX_EVENTS = 50;

interface DashboardState {
  serverStatus: ServerStatus | null;
  instances: Instance[];
  events: ServerEvent[];
  loading: boolean;
  error: string | null;
  connected: boolean;

  fetchServerStatus: () => Promise<void>;
  fetchInstances: () => Promise<void>;
  addEvent: (event: ServerEvent) => void;
  setConnected: (connected: boolean) => void;
}

export const useDashboardStore = create<DashboardState>()((set) => ({
  serverStatus: null,
  instances: [],
  events: [],
  loading: false,
  error: null,
  connected: false,

  fetchServerStatus: async () => {
    try {
      set({ loading: true, error: null });
      const status = await serverApi.status();
      set({ serverStatus: status, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch server status', loading: false });
    }
  },

  fetchInstances: async () => {
    try {
      const data = await instancesApi.list();
      set({ instances: data.instances });
    } catch {
      // silently fail for instance polling
    }
  },

  addEvent: (event) =>
    set((s) => ({
      events: [event, ...s.events].slice(0, MAX_EVENTS),
    })),

  setConnected: (connected) => set({ connected }),
}));
