import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook } from '@testing-library/react';
import { useGlobalEventStream } from '../useGlobalEventStream';

const mockConnect = vi.fn();
const mockDisconnect = vi.fn();

vi.mock('@/stores/useEventStreamStore', () => ({
  useEventStreamStore: vi.fn().mockImplementation((sel?: any) => {
    const state = { connect: mockConnect, disconnect: mockDisconnect, status: 'disconnected' };
    return sel ? sel(state) : state;
  }),
}));

describe('useGlobalEventStream', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('calls connect on mount', () => {
    renderHook(() => useGlobalEventStream());
    expect(mockConnect).toHaveBeenCalled();
  });

  it('calls disconnect on unmount', () => {
    const { unmount } = renderHook(() => useGlobalEventStream());
    unmount();
    expect(mockDisconnect).toHaveBeenCalled();
  });

  it('registers visibilitychange listener', () => {
    const addSpy = vi.spyOn(document, 'addEventListener');
    renderHook(() => useGlobalEventStream());
    expect(addSpy).toHaveBeenCalledWith('visibilitychange', expect.any(Function));
    addSpy.mockRestore();
  });

  it('removes visibilitychange listener on unmount', () => {
    const removeSpy = vi.spyOn(document, 'removeEventListener');
    const { unmount } = renderHook(() => useGlobalEventStream());
    unmount();
    expect(removeSpy).toHaveBeenCalledWith('visibilitychange', expect.any(Function));
    removeSpy.mockRestore();
  });
});
