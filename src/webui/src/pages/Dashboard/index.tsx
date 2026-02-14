import React, { useCallback, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Spin, Alert, Typography, Button, Space } from 'antd';
import { PlusOutlined, RightOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useDashboardStore } from '@/stores/useDashboardStore';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { usePolling } from '@/hooks/usePolling';
import { useEventStream } from '@/hooks/useEventStream';
import { instancesApi } from '@/api/instances';
import { KpiCards } from './components/KpiCards';
import { ActiveInstances } from './components/ActiveInstances';
import { EventFeed } from './components/EventFeed';
import { ProjectCard } from './components/ProjectCard';
import { ServerIndicator } from './components/ServerIndicator';
import styles from './dashboard.module.css';

const SSE_FILTERS = ['instance.started', 'instance.finished', 'schedule.triggered', 'schedule.suppressed'];

export const DashboardPage: React.FC = () => {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const {
    serverStatus,
    instances,
    events,
    loading: dashLoading,
    error: dashError,
    connected,
    fetchServerStatus,
    fetchInstances,
    addEvent,
    setConnected
  } = useDashboardStore();

  const {
    projects,
    runtimes,
    loading: projLoading,
    error: projError,
    fetchProjects,
    fetchRuntimes,
    startProject,
    stopProject
  } = useProjectsStore();

  const loading = dashLoading || projLoading;
  const error = dashError || projError;

  usePolling(
    useCallback(() => {
      fetchServerStatus();
      fetchInstances();
      fetchRuntimes();
    }, [fetchServerStatus, fetchInstances, fetchRuntimes]),
    30000,
  );

  useEffect(() => {
    fetchProjects();
  }, [fetchProjects]);

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

  if (loading && !serverStatus && projects.length === 0) {
    return (
      <div style={{ display: 'grid', placeItems: 'center', height: '60vh' }}>
        <Spin size="large" tip={t('dashboard.loading')} data-testid="dashboard-loading" />
      </div>
    );
  }

  return (
    <div data-testid="page-dashboard">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-end', marginBottom: 40 }}>
        <div>
          <Typography.Title level={2} style={{ margin: 0, letterSpacing: '-1px' }}>{t('dashboard.title')}</Typography.Title>
          <Typography.Text type="secondary" style={{ fontSize: 14 }}>{t('dashboard.subtitle')}</Typography.Text>
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
        <div className={styles.mainPanel}>
          <KpiCards status={serverStatus} />

          {/* Projects Section */}
          <section>
            <div className={styles.sectionHeader}>
              <h3 className={styles.sectionTitle}>{t('dashboard.active_projects')}</h3>
              <Space>
                <Button type="link" size="small" onClick={() => navigate('/projects')}>
                  {t('common.view_all')} <RightOutlined style={{ fontSize: 10 }} />
                </Button>
                <Button
                  type="primary"
                  size="small"
                  icon={<PlusOutlined />}
                  onClick={() => navigate('/projects?action=create')}
                  style={{ borderRadius: 6 }}
                >
                  {t('dashboard.new_project')}
                </Button>
              </Space>
            </div>
            <div className={styles.projectsGrid}>
              {projects.slice(0, 4).map((project) => (
                <ProjectCard
                  key={project.id}
                  project={project}
                  runtime={runtimes[project.id]}
                  onStart={startProject}
                  onStop={stopProject}
                />
              ))}
              {projects.length === 0 && (
                <div className="glass-panel" style={{ gridColumn: '1/-1', padding: 40, textAlign: 'center', color: 'var(--text-tertiary)' }}>
                  {t('dashboard.empty_projects')}
                </div>
              )}
            </div>
          </section>

          {/* Instances Section */}
          <section>
            <div className={styles.sectionHeader}>
              <h3 className={styles.sectionTitle}>{t('dashboard.live_instances')}</h3>
              <Button type="link" size="small" onClick={() => navigate('/instances')}>
                {t('common.view_detail')} <RightOutlined style={{ fontSize: 10 }} />
              </Button>
            </div>
            <ActiveInstances instances={instances} onTerminate={handleTerminate} />
          </section>
        </div>

        <div className={styles.sidePanel}>
          <EventFeed events={events} />
        </div>
      </div>
    </div>
  );
};

export default DashboardPage;
