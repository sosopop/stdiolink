import axios, { AxiosError, AxiosInstance } from 'axios';
import type { ApiError } from '@/types/api';

const apiClient: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || '/api',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' },
});

apiClient.interceptors.response.use(
  (response) => response,
  (error: AxiosError<{ error?: string | { code?: string; message?: string } }>) => {
    const payloadError = error.response?.data?.error;
    const normalizedError =
      typeof payloadError === 'string'
        ? payloadError
        : payloadError?.message || error.message || 'Request failed';

    const apiError: ApiError = {
      error: normalizedError,
      status: error.response?.status || 0,
    };
    return Promise.reject(apiError);
  },
);

export default apiClient;
