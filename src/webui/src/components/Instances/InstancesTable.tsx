import React from 'react';
import { useTranslation } from 'react-i18next';
import { Table, Badge, Popconfirm, Button, Space } from 'antd';
import { useNavigate } from 'react-router-dom';
import type { Instance } from '@/types/instance';

function formatUptime(startedAt: string): string {
  const diff = Math.floor((Date.now() - new Date(startedAt).getTime()) / 1000);
  const h = Math.floor(diff / 3600);
  const m = Math.floor((diff % 3600) / 60);
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m`;
}

const statusMap: Record<string, 'success' | 'processing' | 'error' | 'default'> = {
  running: 'success',
  starting: 'processing',
  stopping: 'processing',
  stopped: 'default',
  failed: 'error',
  crashed: 'error',
};

interface InstancesTableProps {
  instances: Instance[];
  onTerminate: (id: string) => void;
}

export const InstancesTable: React.FC<InstancesTableProps> = ({ instances, onTerminate }) => {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const columns = [
    {
      title: t('instances.table.id'),
      dataIndex: 'id',
      key: 'id',
      ellipsis: true,
      width: 180,
    },
    {
      title: t('instances.table.project'),
      dataIndex: 'projectId',
      key: 'projectId',
      width: 140,
    },
    {
      title: t('instances.table.service'),
      dataIndex: 'serviceId',
      key: 'serviceId',
      width: 140,
    },
    {
      title: t('instances.table.status'),
      dataIndex: 'status',
      key: 'status',
      width: 100,
      render: (status: string) => (
        <Badge status={statusMap[status] || 'default'} text={t(`common.status_${status}`, status)} data-testid="instance-status" />
      ),
    },
    {
      title: t('instances.table.pid'),
      dataIndex: 'pid',
      key: 'pid',
      width: 80,
    },
    {
      title: t('instances.table.uptime'),
      dataIndex: 'startedAt',
      key: 'uptime',
      width: 100,
      defaultSortOrder: 'ascend' as const,
      sorter: (a: Instance, b: Instance) => new Date(a.startedAt).getTime() - new Date(b.startedAt).getTime(),
      render: (startedAt: string) => startedAt ? formatUptime(startedAt) : '--',
    },
    {
      title: t('instances.table.actions'),
      key: 'actions',
      width: 140,
      render: (_: unknown, record: Instance) => (
        <Space>
          <Button size="small" onClick={() => navigate(`/instances/${record.id}`)} data-testid={`detail-${record.id}`}>
            {t('instances.table.detail')}
          </Button>
          <Popconfirm
            title={t('instances.table.terminate_confirm')}
            onConfirm={() => onTerminate(record.id)}
            data-testid={`confirm-terminate-${record.id}`}
          >
            <Button size="small" danger data-testid={`terminate-${record.id}`}>
              {t('common.terminate')}
            </Button>
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <Table
      data-testid="instances-table"
      dataSource={instances}
      columns={columns}
      rowKey="id"
      size="small"
      pagination={{ pageSize: 20 }}
    />
  );
};
