import apiClient from './client';
import type { ServerStatus } from '@/types/server';

export const serverApi = {
  status: () => apiClient.get<ServerStatus>('/server/status').then((r) => r.data),
};
