import apiClient from './client';
import type { DriverListItem, DriverDetail } from '@/types/driver';

export const driversApi = {
  list: () => apiClient.get<{ drivers: DriverListItem[] }>('/drivers').then((r) => r.data),

  detail: (id: string) => apiClient.get<DriverDetail>(`/drivers/${id}`).then((r) => r.data),

  scan: (options?: { refreshMeta?: boolean }) =>
    apiClient.post('/drivers/scan', options ?? {}).then((r) => r.data),

  docs: (id: string, format: string = 'markdown') =>
    apiClient.get<string>(`/drivers/${id}/docs`, { params: { format }, responseType: 'text' as any }).then((r) => r.data),
};
