import { useEffect, useRef } from 'react';
import { useEventStreamStore } from '@/stores/useEventStreamStore';

interface UseSmartPollingOptions {
  fetchFn: () => Promise<void> | void;
  intervalMs: number;
  sseIntervalMs?: number;
  enabled?: boolean;
}

export function useSmartPolling(options: UseSmartPollingOptions): void {
  const sseStatus = useEventStreamStore((s) => s.status);
  const fetchRef = useRef(options.fetchFn);
  fetchRef.current = options.fetchFn;

  const enabled = options.enabled ?? true;
  const interval = sseStatus === 'connected'
    ? options.sseIntervalMs
    : options.intervalMs;

  useEffect(() => {
    if (!enabled || interval === undefined) return;
    fetchRef.current();
    const timer = setInterval(() => fetchRef.current(), interval);
    return () => clearInterval(timer);
  }, [interval, enabled]);
}
