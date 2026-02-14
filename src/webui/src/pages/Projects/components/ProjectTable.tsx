import React from 'react';
import { Table, Tag, Button, Space, Switch, Popconfirm } from 'antd';
import { PlayCircleOutlined, StopOutlined, EyeOutlined, DeleteOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Project, ProjectRuntime } from '@/types/project';

interface ProjectTableProps {
  projects: Project[];
  runtimes: Record<string, ProjectRuntime>;
  loading?: boolean;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onDelete: (id: string) => void;
  onToggleEnabled: (id: string, enabled: boolean) => void;
}

export const ProjectTable: React.FC<ProjectTableProps> = ({
  projects, runtimes, loading, onStart, onStop, onDelete, onToggleEnabled,
}) => {
  const navigate = useNavigate();

  const getStatus = (p: Project): string => {
    const rt = runtimes[p.id];
    return rt?.status ?? (p.enabled ? 'stopped' : 'disabled');
  };

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
      title: 'Service',
      dataIndex: 'serviceId',
      key: 'serviceId',
      render: (id: string) => <Tag>{id}</Tag>,
    },
    {
      title: 'Status',
      key: 'status',
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        return (
          <span data-testid={`status-${record.id}`}>
            <StatusDot status={status === 'running' ? 'running' : status === 'error' || status === 'invalid' ? 'error' : 'stopped'} />
            {' '}{status}
          </span>
        );
      },
    },
    {
      title: 'Enabled',
      key: 'enabled',
      render: (_: unknown, record: Project) => (
        <Switch
          checked={record.enabled}
          onChange={(v) => onToggleEnabled(record.id, v)}
          size="small"
          data-testid={`enabled-${record.id}`}
        />
      ),
    },
    {
      title: 'Actions',
      key: 'actions',
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        const canStart = record.enabled && record.valid && status !== 'running';
        const canStop = status === 'running';
        return (
          <Space>
            <Button
              type="link"
              size="small"
              icon={<EyeOutlined />}
              onClick={() => navigate(`/projects/${record.id}`)}
              data-testid={`view-${record.id}`}
            />
            {canStart && (
              <Button
                type="link"
                size="small"
                icon={<PlayCircleOutlined />}
                onClick={() => onStart(record.id)}
                data-testid={`start-${record.id}`}
              />
            )}
            {canStop && (
              <Popconfirm title={`Stop project "${record.name}"?`} onConfirm={() => onStop(record.id)}>
                <Button type="link" size="small" danger icon={<StopOutlined />} data-testid={`stop-${record.id}`} />
              </Popconfirm>
            )}
            <Popconfirm title={`Delete project "${record.id}"?`} onConfirm={() => onDelete(record.id)}>
              <Button type="link" size="small" danger icon={<DeleteOutlined />} data-testid={`delete-${record.id}`} />
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <Table
      dataSource={projects}
      columns={columns}
      rowKey="id"
      loading={loading}
      pagination={{ pageSize: 20 }}
      data-testid="project-table"
    />
  );
};
