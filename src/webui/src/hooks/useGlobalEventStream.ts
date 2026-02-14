import { useEffect } from 'react';
import { useEventStreamStore } from '@/stores/useEventStreamStore';

export function useGlobalEventStream(): void {
  const connect = useEventStreamStore((s) => s.connect);
  const disconnect = useEventStreamStore((s) => s.disconnect);

  useEffect(() => {
    connect();
    return () => disconnect();
  }, [connect, disconnect]);

  useEffect(() => {
    const handleVisibility = () => {
      if (document.hidden) {
        disconnect();
      } else {
        connect();
      }
    };
    document.addEventListener('visibilitychange', handleVisibility);
    return () => document.removeEventListener('visibilitychange', handleVisibility);
  }, [connect, disconnect]);
}
