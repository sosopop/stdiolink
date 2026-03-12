import React, { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useParams, useSearchParams } from 'react-router-dom';
import { Tabs, Spin, Alert, Empty } from 'antd';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';
import { useDashboardStore } from '@/stores/useDashboardStore';
import { ProjectOverview } from './components/ProjectOverview';
import { ProjectConfig } from './components/ProjectConfig';
import { ProjectInstances } from './components/ProjectInstances';
import { ProjectLogs } from './components/ProjectLogs';
import { ProjectSchedule } from './components/ProjectSchedule';

export const ProjectDetailPage: React.FC = () => {
  const { t } = useTranslation();
  const { id } = useParams<{ id: string }>();
  const [searchParams, setSearchParams] = useSearchParams();
  const tab = searchParams.get('tab');
  const {
    currentProject, currentRuntime, loading, error,
    fetchProjectDetail, fetchRuntime, startProject, stopProject, reloadProject, updateProject, setEnabled,
  } = useProjectsStore();
  const { currentService, fetchServiceDetail } = useServicesStore();
  const { serverStatus, fetchServerStatus } = useDashboardStore();

  useEffect(() => {
    if (id) {
      fetchProjectDetail(id);
      fetchRuntime(id);
    }
  }, [id, fetchProjectDetail, fetchRuntime]);

  useEffect(() => {
    if (currentProject?.serviceId) {
      fetchServiceDetail(currentProject.serviceId);
    }
  }, [currentProject?.serviceId, fetchServiceDetail]);

  useEffect(() => {
    if (!serverStatus) {
      fetchServerStatus();
    }
  }, [serverStatus, fetchServerStatus]);

  if (loading) return <Spin data-testid="detail-loading" />;
  if (error) return <Alert type="error" message={error} data-testid="detail-error" />;
  if (!currentProject) return <Empty description={t('projects.detail.not_found')} data-testid="detail-not-found" />;

  const buildUpdatePayload = (patch: Partial<Pick<typeof currentProject, 'config' | 'schedule'>>) => ({
    name: currentProject.name,
    serviceId: currentProject.serviceId,
    enabled: currentProject.enabled,
    config: patch.config ?? currentProject.config,
    schedule: patch.schedule ?? currentProject.schedule,
  });

  const items = [
    {
      key: 'overview',
      label: t('projects.detail.overview'),
      children: (
        <ProjectOverview
          project={currentProject}
          runtime={currentRuntime}
          onStart={() => startProject(currentProject.id)}
          onStop={() => stopProject(currentProject.id)}
          onReload={() => reloadProject(currentProject.id)}
          onToggleEnabled={(enabled) => setEnabled(currentProject.id, enabled)}
        />
      ),
    },
    {
      key: 'config',
      label: t('projects.detail.config'),
      children: (
        <ProjectConfig
          projectId={currentProject.id}
          config={currentProject.config}
          schema={currentService?.configSchemaFields ?? []}
          serviceDir={currentService?.serviceDir ?? null}
          dataRoot={serverStatus?.dataRoot ?? null}
          onSave={(config) => updateProject(currentProject.id, buildUpdatePayload({ config }))}
        />
      ),
    },
    {
      key: 'instances',
      label: t('projects.detail.instances'),
      children: <ProjectInstances projectId={currentProject.id} />,
    },
    {
      key: 'logs',
      label: t('projects.detail.logs'),
      children: <ProjectLogs projectId={currentProject.id} />,
    },
    {
      key: 'schedule',
      label: t('projects.detail.schedule'),
      children: (
        <ProjectSchedule
          schedule={currentProject.schedule}
          onSave={(schedule) => updateProject(currentProject.id, buildUpdatePayload({ schedule }))}
        />
      ),
    },
  ];

  return (
    <div data-testid="page-project-detail">
      <h2 style={{ marginBottom: 16 }}>{currentProject.name}</h2>
      <Tabs
        activeKey={tab || 'overview'}
        items={items}
        onChange={(key) => setSearchParams({ tab: key })}
        data-testid="detail-tabs"
      />
    </div>
  );
};

export default ProjectDetailPage;
