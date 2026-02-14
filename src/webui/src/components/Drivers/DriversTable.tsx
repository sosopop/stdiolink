import React from 'react';
import { Table, Button, Space, Empty, Typography, Tag } from 'antd';
import { useNavigate } from 'react-router-dom';
import { ApiOutlined, ExperimentOutlined } from '@ant-design/icons';
import type { DriverListItem } from '@/types/driver';

interface DriversTableProps {
  drivers: DriverListItem[];
}

const { Text } = Typography;

export const DriversTable: React.FC<DriversTableProps> = ({ drivers }) => {
  const navigate = useNavigate();

  if (drivers.length === 0) {
    return (
      <div className="glass-panel" style={{ padding: 48, textAlign: 'center' }}>
        <Empty
          image={Empty.PRESENTED_IMAGE_SIMPLE}
          description={<Text type="secondary">No drivers found. Try scanning the drivers directory.</Text>}
          data-testid="empty-drivers"
        />
      </div>
    );
  }

  const columns = [
    {
      title: 'Driver ID',
      dataIndex: 'id',
      key: 'id',
      width: 220,
      render: (id: string) => <Text strong style={{ color: 'var(--brand-primary)' }}>{id}</Text>
    },
    {
      title: 'Display Name',
      dataIndex: 'name',
      key: 'name',
      width: 200,
      render: (v: string) => <Text>{v || '--'}</Text>
    },
    {
      title: 'Version',
      dataIndex: 'version',
      key: 'version',
      width: 120,
      render: (v: string) => v ? <Tag bordered={false} style={{ background: 'rgba(255,255,255,0.05)' }}>v{v}</Tag> : '--'
    },
    {
      title: 'Binary Path',
      dataIndex: 'program',
      key: 'program',
      ellipsis: true,
      render: (p: string) => <Text type="secondary" style={{ fontSize: 12, fontFamily: 'var(--font-mono)' }}>{p}</Text>
    },
    {
      title: 'Actions',
      key: 'actions',
      width: 220,
      align: 'right' as const,
      render: (_: unknown, record: DriverListItem) => (
        <Space size={8}>
          <Button
            size="small"
            icon={<ApiOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/drivers/${record.id}`); }}
            data-testid={`detail-${record.id}`}
          >
            Detail
          </Button>
          <Button
            size="small"
            icon={<ExperimentOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/driverlab?driverId=${record.id}`); }}
            data-testid={`test-${record.id}`}
          >
            Test Lab
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
