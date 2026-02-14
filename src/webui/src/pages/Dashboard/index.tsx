import { Spin, Alert } from 'antd';
import { useCallback } from 'react';
import { useDashboardStore } from '@/stores/useDashboardStore';
import { usePolling } from '@/hooks/usePolling';
import { useEventStream } from '@/hooks/useEventStream';
import { instancesApi } from '@/api/instances';
import { KpiCards } from './components/KpiCards';
import { ActiveInstances } from './components/ActiveInstances';
import { EventFeed } from './components/EventFeed';
import { ServerIndicator } from './components/ServerIndicator';
import styles from './dashboard.module.css';

const SSE_FILTERS = ['instance.started', 'instance.finished', 'schedule.triggered', 'schedule.suppressed'];

export const DashboardPage: React.FC = () => {
  const { serverStatus, instances, events, loading, error, connected, fetchServerStatus, fetchInstances, addEvent, setConnected } =
    useDashboardStore();

  usePolling(
    useCallback(() => {
      fetchServerStatus();
      fetchInstances();
    }, [fetchServerStatus, fetchInstances]),
    30000,
  );

  useEventStream(
    SSE_FILTERS,
    useCallback((e) => addEvent(e), [addEvent]),
    useCallback(() => setConnected(true), [setConnected]),
    useCallback(() => setConnected(false), [setConnected]),
  );

  const handleTerminate = useCallback(async (id: string) => {
    await instancesApi.terminate(id);
    fetchInstances();
  }, [fetchInstances]);

  if (loading && !serverStatus) {
    return <Spin data-testid="dashboard-loading" style={{ display: 'block', margin: '80px auto' }} size="large" />;
  }

  return (
    <div data-testid="page-dashboard">
      <ServerIndicator status={serverStatus} connected={connected} />
      {error && <Alert type="error" message={error} showIcon style={{ marginBottom: 16 }} data-testid="dashboard-error" />}
      <div className={styles.grid}>
        <KpiCards status={serverStatus} />
        <ActiveInstances instances={instances} onTerminate={handleTerminate} />
        <EventFeed events={events} />
      </div>
    </div>
  );
};

export default DashboardPage;
