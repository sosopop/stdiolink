import { create } from 'zustand';
import { driversApi } from '@/api/drivers';
import type { DriverListItem, DriverDetail } from '@/types/driver';

interface DriversState {
  drivers: DriverListItem[];
  currentDriver: DriverDetail | null;
  docsMarkdown: string | null;
  docsLoading: boolean;
  loading: boolean;
  error: string | null;

  fetchDrivers: () => Promise<void>;
  fetchDriverDetail: (id: string) => Promise<void>;
  fetchDriverDocs: (id: string, format?: string) => Promise<string>;
  scanDrivers: () => Promise<void>;
}

export const useDriversStore = create<DriversState>()((set, get) => ({
  drivers: [],
  currentDriver: null,
  docsMarkdown: null,
  docsLoading: false,
  loading: false,
  error: null,

  fetchDrivers: async () => {
    try {
      set({ loading: true, error: null });
      const data = await driversApi.list();
      set({ drivers: data.drivers, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch drivers', loading: false });
    }
  },

  fetchDriverDetail: async (id) => {
    try {
      set({ loading: true, error: null });
      const detail = await driversApi.detail(id);
      set({ currentDriver: detail, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch driver detail', loading: false });
    }
  },

  fetchDriverDocs: async (id, format = 'markdown') => {
    try {
      set({ docsLoading: true, error: null });
      const content = await driversApi.docs(id, format);
      if (format === 'markdown') {
        set({ docsMarkdown: content, docsLoading: false });
      } else {
        set({ docsLoading: false });
      }
      return content;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch docs', docsLoading: false, docsMarkdown: null });
      throw e;
    }
  },

  scanDrivers: async () => {
    try {
      set({ loading: true, error: null });
      await driversApi.scan({ refreshMeta: true });
      await get().fetchDrivers();
    } catch (e: any) {
      set({ error: e?.error || 'Failed to scan drivers', loading: false });
    }
  },
}));
