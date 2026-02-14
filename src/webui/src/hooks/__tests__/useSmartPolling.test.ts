import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { renderHook } from '@testing-library/react';
import { useSmartPolling } from '../useSmartPolling';

vi.mock('@/stores/useEventStreamStore', () => ({
  useEventStreamStore: vi.fn(),
}));

import { useEventStreamStore } from '@/stores/useEventStreamStore';

describe('useSmartPolling', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.clearAllMocks();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('polls at intervalMs when SSE disconnected', () => {
    vi.mocked(useEventStreamStore).mockImplementation((sel?: any) =>
      sel ? sel({ status: 'disconnected' }) : { status: 'disconnected' },
    );
    const fetchFn = vi.fn().mockResolvedValue(undefined);
    renderHook(() => useSmartPolling({ fetchFn, intervalMs: 5000 }));
    expect(fetchFn).toHaveBeenCalledTimes(1); // initial call
    vi.advanceTimersByTime(5000);
    expect(fetchFn).toHaveBeenCalledTimes(2);
  });

  it('polls at sseIntervalMs when SSE connected', () => {
    vi.mocked(useEventStreamStore).mockImplementation((sel?: any) =>
      sel ? sel({ status: 'connected' }) : { status: 'connected' },
    );
    const fetchFn = vi.fn().mockResolvedValue(undefined);
    renderHook(() => useSmartPolling({ fetchFn, intervalMs: 30000, sseIntervalMs: 60000 }));
    expect(fetchFn).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(30000);
    expect(fetchFn).toHaveBeenCalledTimes(1); // not at 30s
    vi.advanceTimersByTime(30000);
    expect(fetchFn).toHaveBeenCalledTimes(2); // at 60s
  });

  it('stops polling when SSE connected and no sseIntervalMs', () => {
    vi.mocked(useEventStreamStore).mockImplementation((sel?: any) =>
      sel ? sel({ status: 'connected' }) : { status: 'connected' },
    );
    const fetchFn = vi.fn().mockResolvedValue(undefined);
    renderHook(() => useSmartPolling({ fetchFn, intervalMs: 5000 }));
    expect(fetchFn).toHaveBeenCalledTimes(0); // no initial call when interval is undefined
    vi.advanceTimersByTime(10000);
    expect(fetchFn).toHaveBeenCalledTimes(0);
  });

  it('does not poll when enabled is false', () => {
    vi.mocked(useEventStreamStore).mockImplementation((sel?: any) =>
      sel ? sel({ status: 'disconnected' }) : { status: 'disconnected' },
    );
    const fetchFn = vi.fn().mockResolvedValue(undefined);
    renderHook(() => useSmartPolling({ fetchFn, intervalMs: 5000, enabled: false }));
    expect(fetchFn).toHaveBeenCalledTimes(0);
    vi.advanceTimersByTime(10000);
    expect(fetchFn).toHaveBeenCalledTimes(0);
  });
});
