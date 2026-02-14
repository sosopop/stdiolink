import React from 'react';
import { Table, Button, Space, Empty } from 'antd';
import { useNavigate } from 'react-router-dom';
import type { DriverListItem } from '@/types/driver';

interface DriversTableProps {
  drivers: DriverListItem[];
}

export const DriversTable: React.FC<DriversTableProps> = ({ drivers }) => {
  const navigate = useNavigate();

  if (drivers.length === 0) {
    return <Empty description="No drivers found. Try scanning for drivers." data-testid="empty-drivers" />;
  }

  const columns = [
    { title: 'ID', dataIndex: 'id', key: 'id', width: 200 },
    { title: 'Name', dataIndex: 'name', key: 'name', width: 180, render: (v: string) => v || '--' },
    { title: 'Version', dataIndex: 'version', key: 'version', width: 100, render: (v: string) => v || '--' },
    { title: 'Program', dataIndex: 'program', key: 'program', ellipsis: true },
    {
      title: 'Actions',
      key: 'actions',
      width: 200,
      render: (_: unknown, record: DriverListItem) => (
        <Space>
          <Button size="small" onClick={() => navigate(`/drivers/${record.id}`)} data-testid={`detail-${record.id}`}>
            Detail
          </Button>
          <Button size="small" onClick={() => navigate(`/driverlab?driverId=${record.id}`)} data-testid={`test-${record.id}`}>
            Test
          </Button>
        </Space>
      ),
    },
  ];

  return (
    <Table
      data-testid="drivers-table"
      dataSource={drivers}
      columns={columns}
      rowKey="id"
      size="small"
      pagination={{ pageSize: 20 }}
    />
  );
};
