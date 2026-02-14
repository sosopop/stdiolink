import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { renderHook } from '@testing-library/react';
import { usePolling } from '@/hooks/usePolling';

describe('usePolling', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('calls callback immediately on mount', () => {
    const cb = vi.fn();
    renderHook(() => usePolling(cb, 5000));
    expect(cb).toHaveBeenCalledTimes(1);
  });

  it('calls callback again after interval', () => {
    const cb = vi.fn();
    renderHook(() => usePolling(cb, 5000));
    expect(cb).toHaveBeenCalledTimes(1);

    vi.advanceTimersByTime(5000);
    expect(cb).toHaveBeenCalledTimes(2);

    vi.advanceTimersByTime(5000);
    expect(cb).toHaveBeenCalledTimes(3);
  });

  it('clears interval on unmount', () => {
    const cb = vi.fn();
    const { unmount } = renderHook(() => usePolling(cb, 5000));
    expect(cb).toHaveBeenCalledTimes(1);

    unmount();
    vi.advanceTimersByTime(10000);
    expect(cb).toHaveBeenCalledTimes(1);
  });
});
