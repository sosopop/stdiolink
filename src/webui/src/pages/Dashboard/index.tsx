import React, { useCallback } from 'react';
import { Spin, Alert, Typography } from 'antd';
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
  const { 
    serverStatus, 
    instances, 
    events, 
    loading, 
    error, 
    connected, 
    fetchServerStatus, 
    fetchInstances, 
    addEvent, 
    setConnected 
  } = useDashboardStore();

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
    return (
      <div style={{ display: 'grid', placeItems: 'center', height: '60vh' }}>
        <Spin size="large" tip="Loading mission control..." data-testid="dashboard-loading" />
      </div>
    );
  }

  return (
    <div data-testid="page-dashboard">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end', marginBottom: 32 }}>
        <div>
          <Typography.Title level={2} style={{ margin: 0, letterSpacing: '-1px' }}>Dashboard</Typography.Title>
          <Typography.Text type="secondary">System overview and real-time telemetry</Typography.Text>
        </div>
        <ServerIndicator status={serverStatus} connected={connected} />
      </div>

      {error && (
        <Alert 
          type="error" 
          message={error} 
          showIcon 
          style={{ marginBottom: 24, borderRadius: 'var(--radius-md)' }} 
          data-testid="dashboard-error" 
        />
      )}

      <div className={styles.grid}>
        <KpiCards status={serverStatus} />
        <div className={styles.mainPanel}>
          <ActiveInstances instances={instances} onTerminate={handleTerminate} />
        </div>
        <div className={styles.sidePanel}>
          <EventFeed events={events} />
        </div>
      </div>
    </div>
  );
};

export default DashboardPage;
