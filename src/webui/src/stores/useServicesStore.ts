import { create } from 'zustand';
import { servicesApi } from '@/api/services';
import type { ServiceInfo, ServiceDetail } from '@/types/service';

interface ServicesState {
  services: ServiceInfo[];
  currentService: ServiceDetail | null;
  loading: boolean;
  error: string | null;

  fetchServices: () => Promise<void>;
  fetchServiceDetail: (id: string) => Promise<void>;
  createService: (data: { id: string; template?: string }) => Promise<boolean>;
  deleteService: (id: string) => Promise<boolean>;
  scanServices: () => Promise<void>;
}

export const useServicesStore = create<ServicesState>()((set, get) => ({
  services: [],
  currentService: null,
  loading: false,
  error: null,

  fetchServices: async () => {
    try {
      set({ loading: true, error: null });
      const data = await servicesApi.list();
      set({ services: data.services, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch services', loading: false });
    }
  },

  fetchServiceDetail: async (id: string) => {
    try {
      set({ loading: true, error: null });
      const detail = await servicesApi.detail(id);
      set({ currentService: detail, loading: false });
    } catch (e: any) {
      set({ error: e?.error || 'Failed to fetch service detail', loading: false });
    }
  },

  createService: async (data) => {
    try {
      set({ error: null });
      await servicesApi.create({ id: data.id, name: data.id, version: '1.0.0', template: data.template as any });
      await get().fetchServices();
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to create service' });
      return false;
    }
  },

  deleteService: async (id: string) => {
    try {
      set({ error: null });
      await servicesApi.delete(id);
      set((s) => ({ services: s.services.filter((svc) => svc.id !== id) }));
      return true;
    } catch (e: any) {
      set({ error: e?.error || 'Failed to delete service' });
      return false;
    }
  },

  scanServices: async () => {
    try {
      set({ loading: true, error: null });
      await servicesApi.scan();
      await get().fetchServices();
    } catch (e: any) {
      set({ error: e?.error || 'Failed to scan services', loading: false });
    }
  },
}));
