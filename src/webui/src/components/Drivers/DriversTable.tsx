import React from 'react';
import { useTranslation } from 'react-i18next';
import { Table, Button, Space, Empty, Typography, Tag } from 'antd';
import { useNavigate } from 'react-router-dom';
import { ApiOutlined, ExperimentOutlined } from '@ant-design/icons';
import type { DriverListItem } from '@/types/driver';

interface DriversTableProps {
  drivers: DriverListItem[];
}

const { Text } = Typography;

export const DriversTable: React.FC<DriversTableProps> = ({ drivers }) => {
  const { t } = useTranslation();
  const navigate = useNavigate();

  if (drivers.length === 0) {
    return (
      <div className="glass-panel" style={{ padding: 48, textAlign: 'center' }}>
        <Empty
          image={Empty.PRESENTED_IMAGE_SIMPLE}
          description={<Text type="secondary">{t('drivers.empty')}</Text>}
          data-testid="empty-drivers"
        />
      </div>
    );
  }

  const columns = [
    {
      title: t('drivers.table.driver'),
      key: 'driver',
      width: 260,
      render: (_: unknown, record: DriverListItem) => (
        <Space size={12}>
          <div style={{ width: 36, height: 36, background: 'rgba(99, 102, 241, 0.1)', borderRadius: 8, display: 'grid', placeItems: 'center', border: '1px solid rgba(99, 102, 241, 0.2)' }}>
            <ApiOutlined style={{ color: 'var(--brand-primary)', fontSize: 18 }} />
          </div>
          <Space direction="vertical" size={0}>
            <Text strong style={{ fontSize: 15, color: 'var(--text-primary)' }}>{record.name || record.id}</Text>
            {record.name && <Text type="secondary" style={{ fontSize: 11, fontFamily: 'var(--font-mono)' }}>{record.id}</Text>}
          </Space>
        </Space>
      )
    },
    {
      title: t('drivers.table.version'),
      dataIndex: 'version',
      key: 'version',
      width: 100,
      render: (v: string) => v ? (
        <Tag bordered={false} style={{ background: 'rgba(255,255,255,0.05)', color: 'var(--text-secondary)', borderRadius: 4, padding: '0 8px' }}>
          v{v}
        </Tag>
      ) : <Text type="secondary" style={{ fontSize: 12 }}>--</Text>
    },
    {
      title: t('drivers.table.executable_path'),
      dataIndex: 'program',
      key: 'program',
      ellipsis: true,
      render: (p: string) => (
        <Text type="secondary" style={{ fontSize: 12, fontFamily: 'var(--font-mono)', color: 'var(--text-tertiary)' }} title={p}>
          {p}
        </Text>
      )
    },
    {
      title: t('drivers.table.actions'),
      key: 'actions',
      width: 200,
      align: 'right' as const,
      render: (_: unknown, record: DriverListItem) => (
        <Space size={4}>
          <Button
            type="text"
            size="small"
            icon={<ApiOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/drivers/${record.id}`); }}
            data-testid={`detail-${record.id}`}
          >
            {t('common.details')}
          </Button>
          <div style={{ width: 1, height: 14, background: 'var(--surface-border)', margin: '0 4px' }} />
          <Button
            type="text"
            size="small"
            icon={<ExperimentOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/driverlab?driverId=${record.id}`); }}
            data-testid={`test-${record.id}`}
          >
            {t('common.lab')}
          </Button>
        </Space>
      ),
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: '0 0 8px 0', overflow: 'hidden' }}>
      <Table
        data-testid="drivers-table"
        dataSource={drivers}
        columns={columns}
        rowKey="id"
        size="small"
        pagination={{ pageSize: 20, style: { marginRight: 24 } }}
        onRow={(record) => ({
          onClick: () => navigate(`/drivers/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
        })}
      />
    </div>
  );
};
