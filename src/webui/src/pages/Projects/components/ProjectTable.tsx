import React from 'react';
import { Table, Tag, Button, Space, Switch, Popconfirm, Typography } from 'antd';
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

const { Text } = Typography;

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
      title: 'Project ID',
      dataIndex: 'id',
      key: 'id',
      render: (id: string) => (
        <Text strong style={{ color: 'var(--brand-primary)', cursor: 'pointer' }} onClick={() => navigate(`/projects/${id}`)}>
          {id}
        </Text>
      ),
    },
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
      render: (name: string) => <Text>{name}</Text>
    },
    {
      title: 'Service',
      dataIndex: 'serviceId',
      key: 'serviceId',
      render: (id: string) => <Tag bordered={false} style={{ background: 'rgba(255,255,255,0.05)', color: 'var(--text-secondary)' }}>{id}</Tag>,
    },
    {
      title: 'Status',
      key: 'status',
      width: 140,
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        const dotStatus = status === 'running' ? 'running' : status === 'error' || status === 'invalid' ? 'error' : 'stopped';
        return (
          <Space data-testid={`status-${record.id}`} size={8}>
            <StatusDot status={dotStatus} />
            <Text style={{ fontSize: 13, textTransform: 'capitalize' }}>{status}</Text>
          </Space>
        );
      },
    },
    {
      title: 'Enabled',
      key: 'enabled',
      width: 100,
      align: 'center' as const,
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
      align: 'right' as const,
      width: 160,
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        const canStart = record.enabled && record.valid && status !== 'running';
        const canStop = status === 'running';
        return (
          <Space size={0}>
            <Button
              type="text"
              size="small"
              icon={<EyeOutlined />}
              onClick={(e) => { e.stopPropagation(); navigate(`/projects/${record.id}`); }}
              data-testid={`view-${record.id}`}
            />
            {canStart ? (
              <Button
                type="text"
                size="small"
                icon={<PlayCircleOutlined style={{ color: 'var(--color-success)' }} />}
                onClick={(e) => { e.stopPropagation(); onStart(record.id); }}
                data-testid={`start-${record.id}`}
              />
            ) : (
              <Button type="text" size="small" disabled icon={<PlayCircleOutlined style={{ opacity: 0.2 }} />} />
            )}
            {canStop && (
              <Popconfirm 
                title={`Stop project "${record.name}"?`} 
                onConfirm={(e) => { e?.stopPropagation(); onStop(record.id); }}
                onCancel={(e) => e?.stopPropagation()}
              >
                <Button 
                  type="text" 
                  size="small" 
                  danger 
                  icon={<StopOutlined />} 
                  data-testid={`stop-${record.id}`}
                  onClick={(e) => e.stopPropagation()}
                />
              </Popconfirm>
            )}
            <Popconfirm 
              title={`Delete project "${record.id}"?`} 
              onConfirm={(e) => { e?.stopPropagation(); onDelete(record.id); }}
              onCancel={(e) => e?.stopPropagation()}
              okButtonProps={{ danger: true }}
            >
              <Button 
                type="text" 
                size="small" 
                danger 
                icon={<DeleteOutlined />} 
                data-testid={`delete-${record.id}`} 
                onClick={(e) => e.stopPropagation()}
              />
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: '8px 0' }}>
      <Table
        dataSource={projects}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={{ pageSize: 10, style: { marginRight: 24 } }}
        data-testid="project-table"
        onRow={(record) => ({
          onClick: () => navigate(`/projects/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
        })}
      />
    </div>
  );
};
