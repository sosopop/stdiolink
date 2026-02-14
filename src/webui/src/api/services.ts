import apiClient from './client';
import type { ServiceInfo, ServiceDetail, ServiceFile, CreateServiceRequest } from '@/types/service';

export const servicesApi = {
  list: () => apiClient.get<{ services: ServiceInfo[] }>('/services').then((r) => r.data),

  detail: (id: string) => apiClient.get<ServiceDetail>(`/services/${id}`).then((r) => r.data),

  create: (data: CreateServiceRequest) =>
    apiClient.post<ServiceInfo & { created: boolean }>('/services', data).then((r) => r.data),

  delete: (id: string, force = false) =>
    apiClient.delete(`/services/${id}`, { params: force ? { force: 'true' } : undefined }),

  scan: (options?: {
    revalidateProjects?: boolean;
    restartScheduling?: boolean;
    stopInvalidProjects?: boolean;
  }) => apiClient.post('/services/scan', options ?? {}).then((r) => r.data),

  files: (id: string) =>
    apiClient
      .get<{ serviceId: string; serviceDir: string; files: ServiceFile[] }>(
        `/services/${id}/files`,
      )
      .then((r) => r.data),

  fileRead: (id: string, path: string) =>
    apiClient
      .get<{ path: string; content: string; size: number; modifiedAt: string }>(
        `/services/${id}/files/content`,
        { params: { path } },
      )
      .then((r) => r.data),

  fileWrite: (id: string, path: string, content: string) =>
    apiClient
      .put(`/services/${id}/files/content`, { content }, { params: { path } })
      .then((r) => r.data),

  fileCreate: (id: string, path: string, content: string) =>
    apiClient
      .post(`/services/${id}/files/content`, { content }, { params: { path } })
      .then((r) => r.data),

  fileDelete: (id: string, path: string) =>
    apiClient.delete(`/services/${id}/files/content`, { params: { path } }),

  validateSchema: (id: string, schema: Record<string, unknown>) =>
    apiClient
      .post<{ valid: boolean; error?: string }>(`/services/${id}/validate-schema`, { schema })
      .then((r) => r.data),

  generateDefaults: (id: string) =>
    apiClient.post(`/services/${id}/generate-defaults`).then((r) => r.data),

  validateConfig: (id: string, config: Record<string, unknown>) =>
    apiClient
      .post<{ valid: boolean; errors?: { field: string; message: string }[] }>(
        `/services/${id}/validate-config`,
        { config },
      )
      .then((r) => r.data),
};
