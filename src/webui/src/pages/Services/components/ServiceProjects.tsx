import React, { useEffect, useState } from 'react';
import { Table, Tag, Empty } from 'antd';
import { useNavigate } from 'react-router-dom';
import { projectsApi } from '@/api/projects';
import type { Project } from '@/types/project';

interface ServiceProjectsProps {
  serviceId: string;
}

export const ServiceProjects: React.FC<ServiceProjectsProps> = ({ serviceId }) => {
  const [projects, setProjects] = useState<Project[]>([]);
  const [loading, setLoading] = useState(false);
  const navigate = useNavigate();

  useEffect(() => {
    const load = async () => {
      setLoading(true);
      try {
        const data = await projectsApi.list({ serviceId });
        setProjects(data.projects);
      } catch {
        // silently fail
      } finally {
        setLoading(false);
      }
    };
    load();
  }, [serviceId]);

  if (!loading && projects.length === 0) {
    return <Empty description="No projects reference this service" data-testid="no-projects" />;
  }

  const columns = [
    {
      title: 'ID',
      dataIndex: 'id',
      key: 'id',
      render: (id: string) => <a onClick={() => navigate(`/projects/${id}`)}>{id}</a>,
    },
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
    },
    {
      title: 'Status',
      dataIndex: 'status',
      key: 'status',
      render: (s: string) => <Tag color={s === 'running' ? 'green' : 'default'}>{s}</Tag>,
    },
    {
      title: 'Enabled',
      dataIndex: 'enabled',
      key: 'enabled',
      render: (v: boolean) => (v ? <Tag color="green">Yes</Tag> : <Tag>No</Tag>),
    },
  ];

  return (
    <div data-testid="service-projects">
      <Table
        dataSource={projects}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={false}
        size="small"
      />
    </div>
  );
};
