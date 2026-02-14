import axios, { AxiosError, AxiosInstance } from 'axios';
import type { ApiError } from '@/types/api';

const apiClient: AxiosInstance = axios.create({
  baseURL: import.meta.env.VITE_API_BASE_URL || '/api',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' },
});

apiClient.interceptors.response.use(
  (response) => response,
  (error: AxiosError<{ error?: string }>) => {
    const apiError: ApiError = {
      error: error.response?.data?.error || error.message || 'Request failed',
      status: error.response?.status || 0,
    };
    return Promise.reject(apiError);
  },
);

export default apiClient;
