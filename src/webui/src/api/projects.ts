import apiClient from './client';
import type { Project, ProjectRuntime, CreateProjectRequest, UpdateProjectRequest } from '@/types/project';

export const projectsApi = {
  list: (params?: { serviceId?: string; status?: string; enabled?: string; page?: number; pageSize?: number }) =>
    apiClient
      .get<{ projects: Project[]; total: number; page: number; pageSize: number }>('/projects', { params })
      .then((r) => r.data),

  detail: (id: string) => apiClient.get<Project>(`/projects/${id}`).then((r) => r.data),

  create: (data: CreateProjectRequest) =>
    apiClient.post<Project>('/projects', data).then((r) => r.data),

  update: (id: string, data: UpdateProjectRequest) =>
    apiClient.put<Project>(`/projects/${id}`, data).then((r) => r.data),

  delete: (id: string) => apiClient.delete(`/projects/${id}`),

  validate: (id: string, config: Record<string, unknown>) =>
    apiClient
      .post<{ valid: boolean; error?: string }>(`/projects/${id}/validate`, { config })
      .then((r) => r.data),

  start: (id: string) =>
    apiClient.post<{ instanceId: string; pid: number }>(`/projects/${id}/start`).then((r) => r.data),

  stop: (id: string) =>
    apiClient.post<{ stopped: boolean }>(`/projects/${id}/stop`).then((r) => r.data),

  reload: (id: string) => apiClient.post<Project>(`/projects/${id}/reload`).then((r) => r.data),

  runtime: (id: string) =>
    apiClient.get<ProjectRuntime>(`/projects/${id}/runtime`).then((r) => r.data),

  runtimeBatch: (ids?: string[]) =>
    apiClient
      .get<{ runtimes: ProjectRuntime[] }>('/projects/runtime', {
        params: ids?.length ? { ids: ids.join(',') } : undefined,
      })
      .then((r) => r.data),

  setEnabled: (id: string, enabled: boolean) =>
    apiClient.patch<Project>(`/projects/${id}/enabled`, { enabled }).then((r) => r.data),

  logs: (id: string, params?: { lines?: number }) =>
    apiClient
      .get<{ projectId: string; lines: string[]; logPath: string }>(`/projects/${id}/logs`, { params })
      .then((r) => r.data),
};
