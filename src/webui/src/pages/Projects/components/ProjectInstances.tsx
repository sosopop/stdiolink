import React, { useEffect, useState } from 'react';
import { Table, Tag } from 'antd';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import { instancesApi } from '@/api/instances';
import type { Instance } from '@/types/instance';

interface ProjectInstancesProps {
  projectId: string;
}

export const ProjectInstances: React.FC<ProjectInstancesProps> = ({ projectId }) => {
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
      title: 'ID',
      dataIndex: 'id',
      key: 'id',
    },
    {
      title: 'Status',
      dataIndex: 'status',
      key: 'status',
      render: (s: string) => (
        <span>
          <StatusDot status={s === 'running' ? 'running' : s === 'error' ? 'error' : 'stopped'} />
          {' '}{s}
        </span>
      ),
    },
    {
      title: 'PID',
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
