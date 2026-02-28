import React from 'react';
import { useTranslation } from 'react-i18next';
import { Table, Button, Popconfirm, Typography, Space, Tooltip } from 'antd';
import { StopOutlined, EyeOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import styles from '../dashboard.module.css';
import type { Instance } from '@/types/instance';

interface ActiveInstancesProps {
  instances: Instance[];
  onTerminate: (id: string) => void;
}

const { Text } = Typography;

export const ActiveInstances: React.FC<ActiveInstancesProps> = ({ instances, onTerminate }) => {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const columns = [
    {
      title: t('dashboard.table.lifecycle'),
      key: 'lifecycle',
      width: 180,
      render: (_: unknown, record: Instance) => (
        <div style={{ background: 'rgba(255,255,255,0.03)', padding: '4px 12px', borderRadius: 100, display: 'inline-flex', alignItems: 'center', gap: 8, border: '1px solid var(--surface-border)' }}>
          <StatusDot status={record.status === 'running' ? 'running' : record.status === 'error' ? 'error' : 'stopped'} size={8} />
          <Text style={{ fontSize: 11, fontWeight: 700, textTransform: 'uppercase' }}>{t(`common.status_${record.status}`, record.status)}</Text>
        </div>
      ),
    },
    {
      title: t('dashboard.table.target_project'),
      dataIndex: 'projectId',
      key: 'projectId',
      render: (id: string) => (
        <Text strong style={{ fontSize: 14, color: 'var(--brand-primary)' }}>{id}</Text>
      )
    },
    {
      title: t('dashboard.table.system_pid'),
      dataIndex: 'pid',
      key: 'pid',
      width: 120,
      render: (pid: number) => <Text code style={{ fontSize: 12, background: 'var(--bg-code)', padding: '2px 6px', borderRadius: 4, color: 'var(--text-secondary)' }}>{pid}</Text>
    },
    {
      title: t('dashboard.table.uptime'),
      dataIndex: 'startedAt',
      key: 'uptime',
      render: (start: string) => {
        const uptime = Math.floor((Date.now() - new Date(start).getTime()) / 1000);
        const mins = Math.floor(uptime / 60);
        const secs = uptime % 60;
        return <Text type="secondary" style={{ fontSize: 12, fontFamily: 'var(--font-mono)' }}>{mins}m {secs}s</Text>;
      }
    },
    {
      title: '',
      key: 'actions',
      width: 100,
      align: 'right' as const,
      render: (_: unknown, record: Instance) => (
        <Space size={4}>
          <Tooltip title={t('common.view_trace')}>
            <Button
              type="text"
              size="small"
              icon={<EyeOutlined />}
              onClick={() => navigate(`/instances/${record.id}`)}
            />
          </Tooltip>
          <Popconfirm
            title={t('dashboard.terminate_confirm')}
            onConfirm={() => onTerminate(record.id)}
            okText={t('common.terminate')}
            cancelText={t('common.cancel')}
            okButtonProps={{ danger: true }}
          >
            <Button
              type="text"
              size="small"
              icon={<StopOutlined />}
              danger
            />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div className={`glass-panel ${styles.activeInstancesTable}`} style={{ padding: '0 0 8px 0', overflow: 'hidden' }}>
      <Table
        dataSource={instances}
        columns={columns}
        rowKey="id"
        size="small"
        pagination={false}
        onRow={(record) => ({
          onClick: () => navigate(`/instances/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
        })}
        locale={{
          emptyText: (
            <div style={{ padding: '40px 0', textAlign: 'center' }}>
              <Text type="secondary">{t('dashboard.no_instances')}</Text>
            </div>
          )
        }}
      />
    </div>
  );
};
