import apiClient from './client';
import type { Instance, ProcessTreeResponse, ResourcesResponse } from '@/types/instance';

export const instancesApi = {
  list: (params?: { projectId?: string }) =>
    apiClient.get<{ instances: Instance[] }>('/instances', { params }).then((r) => r.data),

  detail: (id: string) => apiClient.get<Instance>(`/instances/${id}`).then((r) => r.data),

  terminate: (id: string) =>
    apiClient.post<{ terminated: boolean }>(`/instances/${id}/terminate`).then((r) => r.data),

  processTree: (id: string) =>
    apiClient.get<ProcessTreeResponse>(`/instances/${id}/process-tree`).then((r) => r.data),

  resources: (id: string, params?: { includeChildren?: boolean }) =>
    apiClient.get<ResourcesResponse>(`/instances/${id}/resources`, { params }).then((r) => r.data),

  logs: (id: string, params?: { lines?: number }) =>
    apiClient
      .get<{ projectId: string; lines: string[] }>(`/instances/${id}/logs`, { params })
      .then((r) => r.data),
};
