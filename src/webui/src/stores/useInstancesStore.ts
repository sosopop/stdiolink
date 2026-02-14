import { create } from 'zustand';
import { instancesApi } from '@/api/instances';
import type { Instance, ProcessTreeNode, ProcessTreeSummary, ProcessInfo } from '@/types/instance';

export interface ResourceSample {
  timestamp: number;
  cpuPercent: number;
  memoryRssBytes: number;
  threadCount: number;
}

interface InstancesState {
  instances: Instance[];
  currentInstance: Instance | null;
  processTree: { tree: ProcessTreeNode; summary: ProcessTreeSummary } | null;
  resources: ProcessInfo[];
  resourceHistory: ResourceSample[];
  logs: string[];
  loading: boolean;
  error: string | null;

  fetchInstances: (params?: { projectId?: string }) => Promise<void>;
  fetchInstanceDetail: (id: string) => Promise<void>;
  fetchProcessTree: (id: string) => Promise<void>;
  fetchResources: (id: string) => Promise<void>;
  fetchLogs: (id: string, params?: { lines?: number }) => Promise<void>;
  terminateInstance: (id: string) => Promise<void>;
  appendResourceSample: (sample: ResourceSample) => void;
}

const MAX_HISTORY = 60;

export const useInstancesStore = create<InstancesState>()((set, get) => ({
  instances: [],
  currentInstance: null,
  processTree: null,
  resources: [],
  resourceHistory: [],
  logs: [],
  loading: false,
  error: null,

  fetchInstances: async (params) => {
    try {
      set({ loading: true, error: null });
      const data = await instancesApi.list(params);
      set({ instances: data.instances, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch instances', loading: false });
    }
  },

  fetchInstanceDetail: async (id) => {
    try {
      set({ loading: true, error: null });
      const instance = await instancesApi.detail(id);
      set({ currentInstance: instance, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch instance detail', loading: false });
    }
  },

  fetchProcessTree: async (id) => {
    try {
      const data = await instancesApi.processTree(id);
      set({ processTree: { tree: data.tree, summary: data.summary } });
    } catch {
      // silently fail
    }
  },

  fetchResources: async (id) => {
    try {
      const data = await instancesApi.resources(id, { includeChildren: true });
      set({ resources: data.processes });
      get().appendResourceSample({
        timestamp: Date.now(),
        cpuPercent: data.summary.totalCpuPercent,
        memoryRssBytes: data.summary.totalMemoryRssBytes,
        threadCount: data.summary.totalThreads,
      });
    } catch {
      // silently fail
    }
  },

  fetchLogs: async (id, params) => {
    try {
      const data = await instancesApi.logs(id, params);
      set({ logs: data.lines });
    } catch {
      // silently fail
    }
  },

  terminateInstance: async (id) => {
    try {
      set({ error: null });
      await instancesApi.terminate(id);
      set((s) => ({ instances: s.instances.filter((i) => i.id !== id) }));
    } catch (e: any) {
      set({ error: e?.error || 'Failed to terminate instance' });
    }
  },

  appendResourceSample: (sample) => {
    set((s) => {
      const history = [...s.resourceHistory, sample];
      if (history.length > MAX_HISTORY) {
        return { resourceHistory: history.slice(history.length - MAX_HISTORY) };
      }
      return { resourceHistory: history };
    });
  },
}));
