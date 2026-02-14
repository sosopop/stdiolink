import React from 'react';
import { useTranslation } from 'react-i18next';
import { Table, Tag, Button, Space, Switch, Popconfirm, Typography } from 'antd';
import { PlayCircleOutlined, StopOutlined, DeleteOutlined, SettingOutlined, ClockCircleOutlined, ThunderboltOutlined, SyncOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Project, ProjectRuntime } from '@/types/project';

interface ProjectTableProps {
  projects: Project[];
  runtimes: Record<string, ProjectRuntime>;
  loading: boolean;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
  onDelete: (id: string) => void;
  onToggleEnabled: (id: string, enabled: boolean) => void;
}

const { Text } = Typography;

export const ProjectTable: React.FC<ProjectTableProps> = ({
  projects, runtimes, loading, onStart, onStop, onDelete, onToggleEnabled,
}) => {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const getStatus = (project: Project) => {
    if (!project.valid) return 'invalid';
    return runtimes[project.id]?.status || 'stopped';
  };

  const columns = [
    {
      title: t('projects.table.details'),
      key: 'details',
      render: (_: unknown, record: Project) => (
        <Space direction="vertical" size={0}>
          <Text strong style={{ color: 'var(--brand-primary)', cursor: 'pointer', fontSize: 15 }} onClick={() => navigate(`/projects/${record.id}`)}>
            {record.name}
          </Text>
          <Text type="secondary" style={{ fontSize: 11, fontFamily: 'var(--font-mono)' }}>{record.id}</Text>
        </Space>
      ),
    },
    {
      title: t('projects.table.service_template'),
      dataIndex: 'serviceId',
      key: 'serviceId',
      render: (id: string) => (
        <Tag bordered={false} style={{ background: 'rgba(255,255,255,0.05)', color: 'var(--text-secondary)', padding: '2px 10px', borderRadius: 6 }}>
          {id}
        </Tag>
      ),
    },
    {
      title: t('projects.table.runtime_status'),
      key: 'status',
      width: 160,
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        const dotStatus = status === 'running' ? 'running' : status === 'invalid' ? 'error' : 'stopped';
        return (
          <div style={{ background: 'rgba(255,255,255,0.03)', padding: '6px 12px', borderRadius: 100, display: 'inline-flex', alignItems: 'center', gap: 10, border: '1px solid var(--surface-border)' }}>
            <StatusDot status={dotStatus} size={10} />
            <Text style={{ fontSize: 12, textTransform: 'uppercase', fontWeight: 700, letterSpacing: '0.5px' }}>{t(`common.status_${status}`, status)}</Text>
          </div>
        );
      },
    },
    {
      title: t('projects.table.schedule'),
      key: 'schedule',
      width: 150,
      render: (_: unknown, record: Project) => {
        const sType = record.schedule?.type;
        const icon = sType === 'daemon' ? <SyncOutlined /> : sType === 'fixed_rate' ? <ClockCircleOutlined /> : <ThunderboltOutlined />;
        const color = sType === 'daemon' ? 'var(--color-success)' : sType === 'fixed_rate' ? 'var(--brand-primary)' : 'var(--text-tertiary)';
        const label = sType === 'fixed_rate' ? 'fixed_rate' : (sType ?? 'manual');
        const detail = sType === 'fixed_rate' && record.schedule?.intervalMs
          ? `${(record.schedule.intervalMs / 1000).toFixed(0)}s`
          : sType === 'daemon' && record.schedule?.restartDelayMs
            ? `${t('projects.table.restart_prefix')} ${(record.schedule.restartDelayMs / 1000).toFixed(0)}s`
            : null;
        return (
          <Space size={6}>
            <span style={{ color, fontSize: 13 }}>{icon}</span>
            <Space direction="vertical" size={0}>
              <Text style={{ fontSize: 12, fontWeight: 600 }}>{label}</Text>
              {detail && <Text type="secondary" style={{ fontSize: 10 }}>{detail}</Text>}
            </Space>
          </Space>
        );
      },
    },
    {
      title: t('projects.table.enabled'),
      key: 'enabled',
      width: 100,
      align: 'center' as const,
      render: (_: unknown, record: Project) => (
        <Switch
          size="small"
          checked={record.enabled}
          onChange={(checked) => onToggleEnabled(record.id, checked)}
          onClick={(_, e) => e.stopPropagation()}
        />
      ),
    },
    {
      title: t('projects.table.actions'),
      key: 'actions',
      align: 'right' as const,
      width: 200,
      render: (_: unknown, record: Project) => {
        const status = getStatus(record);
        const canStart = record.enabled && record.valid && status !== 'running';
        const canStop = status === 'running';
        return (
          <Space size={4}>
            <Button
              type="text"
              icon={<PlayCircleOutlined style={{ color: canStart ? 'var(--color-success)' : 'inherit', opacity: canStart ? 1 : 0.2 }} />}
              disabled={!canStart}
              onClick={(e) => { e.stopPropagation(); onStart(record.id); }}
            />
            <Popconfirm
              title={t('projects.table.stop_confirm')}
              onConfirm={(e) => { e?.stopPropagation(); onStop(record.id); }}
              onCancel={(e) => e?.stopPropagation()}
            >
              <Button
                type="text"
                danger
                icon={<StopOutlined style={{ opacity: canStop ? 1 : 0.2 }} />}
                disabled={!canStop}
                onClick={(e) => e.stopPropagation()}
              />
            </Popconfirm>
            <div style={{ width: 1, height: 16, background: 'var(--surface-border)', margin: '0 4px' }} />
            <Button
              type="text"
              icon={<SettingOutlined />}
              onClick={(e) => { e.stopPropagation(); navigate(`/projects/${record.id}`); }}
            />
            <Popconfirm
              title={t('projects.table.delete_confirm')}
              onConfirm={(e) => { e?.stopPropagation(); onDelete(record.id); }}
              onCancel={(e) => e?.stopPropagation()}
              okButtonProps={{ danger: true }}
            >
              <Button
                type="text"
                danger
                icon={<DeleteOutlined />}
                onClick={(e) => e.stopPropagation()}
              />
            </Popconfirm>
          </Space>
        );
      },
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: '0 0 16px 0', overflow: 'hidden' }}>
      <Table
        dataSource={projects}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={{
          pageSize: 10,
          style: { marginRight: 24, marginTop: 24 },
          showTotal: (total) => <Text type="secondary" style={{ fontSize: 12 }}>{t('projects.table.total_count', { count: total })}</Text>
        }}
        onRow={(record) => ({
          onClick: () => navigate(`/projects/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
        })}
      />
    </div>
  );
};
