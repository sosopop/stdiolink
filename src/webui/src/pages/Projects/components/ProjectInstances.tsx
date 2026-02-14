import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Table, Tag } from 'antd';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import { instancesApi } from '@/api/instances';
import type { Instance } from '@/types/instance';

interface ProjectInstancesProps {
  projectId: string;
}

export const ProjectInstances: React.FC<ProjectInstancesProps> = ({ projectId }) => {
  const { t } = useTranslation();
  const [instances, setInstances] = useState<Instance[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    const load = async () => {
      setLoading(true);
      try {
        const data = await instancesApi.list();
        setInstances(data.instances.filter((i) => i.projectId === projectId));
      } catch {
        // silently fail
      } finally {
        setLoading(false);
      }
    };
    load();
  }, [projectId]);

  const columns = [
    {
      title: t('projects.instances_tab.id'),
      dataIndex: 'id',
      key: 'id',
    },
    {
      title: t('projects.instances_tab.status'),
      dataIndex: 'status',
      key: 'status',
      render: (s: string) => (
        <span>
          <StatusDot status={s === 'running' ? 'running' : s === 'error' ? 'error' : 'stopped'} />
          {' '}{t(`common.status_${s}`, s)}
        </span>
      ),
    },
    {
      title: t('projects.instances_tab.pid'),
      dataIndex: 'pid',
      key: 'pid',
      render: (v: number) => <Tag>{v}</Tag>,
    },
  ];

  return (
    <div data-testid="project-instances">
      <Table
        dataSource={instances}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={false}
        size="small"
      />
    </div>
  );
};
