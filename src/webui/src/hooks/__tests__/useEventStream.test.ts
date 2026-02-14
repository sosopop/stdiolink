import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook } from '@testing-library/react';
import { useEventStream } from '@/hooks/useEventStream';
import { EventStream } from '@/api/event-stream';

vi.mock('@/api/event-stream', () => {
  const MockEventStream = vi.fn().mockImplementation(() => ({
    connect: vi.fn(),
    close: vi.fn(),
    on: vi.fn(),
    off: vi.fn(),
  }));
  return { EventStream: MockEventStream };
});

describe('useEventStream', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('connects on mount', () => {
    const onEvent = vi.fn();
    renderHook(() => useEventStream(['instance.started'], onEvent));

    const instance = vi.mocked(EventStream).mock.results[0]!.value;
    expect(instance.connect).toHaveBeenCalledWith(['instance.started']);
    expect(instance.on).toHaveBeenCalledWith('event', expect.any(Function));
  });

  it('closes on unmount', () => {
    const onEvent = vi.fn();
    const { unmount } = renderHook(() => useEventStream(['instance.started'], onEvent));

    const instance = vi.mocked(EventStream).mock.results[0]!.value;
    unmount();
    expect(instance.close).toHaveBeenCalled();
  });

  it('registers connected and error callbacks', () => {
    const onEvent = vi.fn();
    const onConnected = vi.fn();
    const onError = vi.fn();
    renderHook(() => useEventStream(['instance.started'], onEvent, onConnected, onError));

    const instance = vi.mocked(EventStream).mock.results[0]!.value;
    expect(instance.on).toHaveBeenCalledWith('connected', expect.any(Function));
    expect(instance.on).toHaveBeenCalledWith('error', expect.any(Function));
  });
});
