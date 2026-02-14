import { describe, it, expect, vi, beforeEach } from 'vitest';
import axios, { AxiosError } from 'axios';

vi.mock('axios', async () => {
  const actual = await vi.importActual<typeof import('axios')>('axios');
  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    patch: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  };
  return {
    ...actual,
    default: {
      create: vi.fn(() => instance),
    },
  };
});

function getMockInstance() {
  return (axios.create as ReturnType<typeof vi.fn>).mock.results[0]!.value;
}

describe('apiClient', () => {
  beforeEach(() => {
    vi.resetModules();
  });

  it('creates axios instance with correct config', async () => {
    await import('../client');
    expect(axios.create).toHaveBeenCalledWith({
      baseURL: '/api',
      timeout: 30000,
      headers: { 'Content-Type': 'application/json' },
    });
  });

  it('registers response interceptor', async () => {
    await import('../client');
    const instance = getMockInstance();
    expect(instance.interceptors.response.use).toHaveBeenCalledWith(
      expect.any(Function),
      expect.any(Function),
    );
  });

  it('error interceptor transforms AxiosError to ApiError', async () => {
    await import('../client');
    const instance = getMockInstance();
    const [, errorHandler] = instance.interceptors.response.use.mock.calls[0];

    const axiosError = {
      response: {
        data: { error: 'Not found' },
        status: 404,
      },
      message: 'Request failed',
      isAxiosError: true,
    } as unknown as AxiosError;

    await expect(errorHandler(axiosError)).rejects.toEqual({
      error: 'Not found',
      status: 404,
    });
  });

  it('error interceptor handles missing response', async () => {
    await import('../client');
    const instance = getMockInstance();
    const [, errorHandler] = instance.interceptors.response.use.mock.calls[0];

    const axiosError = {
      response: undefined,
      message: 'Network Error',
      isAxiosError: true,
    } as unknown as AxiosError;

    await expect(errorHandler(axiosError)).rejects.toEqual({
      error: 'Network Error',
      status: 0,
    });
  });

  it('success interceptor passes response through', async () => {
    await import('../client');
    const instance = getMockInstance();
    const [successHandler] = instance.interceptors.response.use.mock.calls[0];

    const response = { data: { ok: true }, status: 200 };
    expect(successHandler(response)).toBe(response);
  });
});
