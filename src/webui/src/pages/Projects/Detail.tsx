import React, { useEffect } from 'react';
import { useParams } from 'react-router-dom';
import { Tabs, Spin, Alert, Empty } from 'antd';
import { useProjectsStore } from '@/stores/useProjectsStore';
import { useServicesStore } from '@/stores/useServicesStore';
import { ProjectOverview } from './components/ProjectOverview';
import { ProjectConfig } from './components/ProjectConfig';
import { ProjectInstances } from './components/ProjectInstances';
import { ProjectLogs } from './components/ProjectLogs';
import { ProjectSchedule } from './components/ProjectSchedule';

export const ProjectDetailPage: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const {
    currentProject, currentRuntime, loading, error,
    fetchProjectDetail, fetchRuntime, startProject, stopProject, reloadProject, updateProject,
  } = useProjectsStore();
  const { currentService, fetchServiceDetail } = useServicesStore();

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

  if (loading) return <Spin data-testid="detail-loading" />;
  if (error) return <Alert type="error" message={error} data-testid="detail-error" />;
  if (!currentProject) return <Empty description="Project not found" data-testid="detail-not-found" />;

  const items = [
    {
      key: 'overview',
      label: 'Overview',
      children: (
        <ProjectOverview
          project={currentProject}
          runtime={currentRuntime}
          onStart={() => startProject(currentProject.id)}
          onStop={() => stopProject(currentProject.id)}
          onReload={() => reloadProject(currentProject.id)}
        />
      ),
    },
    {
      key: 'config',
      label: 'Config',
      children: (
        <ProjectConfig
          config={currentProject.config}
          schema={currentService?.configSchemaFields ?? []}
          onSave={(config) => updateProject(currentProject.id, { config })}
        />
      ),
    },
    {
      key: 'instances',
      label: 'Instances',
      children: <ProjectInstances projectId={currentProject.id} />,
    },
    {
      key: 'logs',
      label: 'Logs',
      children: <ProjectLogs projectId={currentProject.id} />,
    },
    {
      key: 'schedule',
      label: 'Schedule',
      children: (
        <ProjectSchedule
          schedule={currentProject.schedule}
          onSave={(schedule) => updateProject(currentProject.id, { schedule })}
        />
      ),
    },
  ];

  return (
    <div data-testid="page-project-detail">
      <h2 style={{ marginBottom: 16 }}>{currentProject.name}</h2>
      <Tabs items={items} data-testid="detail-tabs" />
    </div>
  );
};

export default ProjectDetailPage;
