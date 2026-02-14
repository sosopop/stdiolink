import { useEffect, useRef, useCallback } from 'react';
import { EventStream } from '@/api/event-stream';
import type { ServerEvent } from '@/types/server';

export function useEventStream(
  filters: string[],
  onEvent: (event: ServerEvent) => void,
  onConnected?: () => void,
  onError?: () => void,
) {
  const streamRef = useRef<EventStream | null>(null);
  const onEventRef = useRef(onEvent);
  const onConnectedRef = useRef(onConnected);
  const onErrorRef = useRef(onError);
  onEventRef.current = onEvent;
  onConnectedRef.current = onConnected;
  onErrorRef.current = onError;

  const stableFilters = useCallback(() => filters, [filters.join(',')]);

  useEffect(() => {
    const stream = new EventStream();
    streamRef.current = stream;

    stream.on('event', (e) => onEventRef.current(e));
    stream.on('connected', () => onConnectedRef.current?.());
    stream.on('error', () => onErrorRef.current?.());
    stream.connect(stableFilters());

    return () => {
      stream.close();
      streamRef.current = null;
    };
  }, [stableFilters]);
}
