import React from 'react';
import { Table, Button, Space, Popconfirm, Tag } from 'antd';
import { EyeOutlined, DeleteOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import type { ServiceInfo } from '@/types/service';

interface ServiceTableProps {
  services: ServiceInfo[];
  loading?: boolean;
  onDelete: (id: string) => void;
}

export const ServiceTable: React.FC<ServiceTableProps> = ({ services, loading, onDelete }) => {
  const navigate = useNavigate();

  const columns = [
    {
      title: 'ID',
      dataIndex: 'id',
      key: 'id',
      render: (id: string) => <a onClick={() => navigate(`/services/${id}`)}>{id}</a>,
    },
    {
      title: 'Name',
      dataIndex: 'name',
      key: 'name',
    },
    {
      title: 'Version',
      dataIndex: 'version',
      key: 'version',
      render: (v: string) => <Tag>{v}</Tag>,
    },
    {
      title: 'Projects',
      dataIndex: 'projectCount',
      key: 'projectCount',
    },
    {
      title: 'Actions',
      key: 'actions',
      render: (_: unknown, record: ServiceInfo) => (
        <Space>
          <Button
            type="link"
            size="small"
            icon={<EyeOutlined />}
            data-testid={`view-${record.id}`}
            onClick={() => navigate(`/services/${record.id}`)}
          />
          <Popconfirm
            title={`Delete service "${record.id}"?`}
            description={record.projectCount > 0 ? `This service has ${record.projectCount} project(s).` : undefined}
            onConfirm={() => onDelete(record.id)}
          >
            <Button type="link" danger size="small" icon={<DeleteOutlined />} data-testid={`delete-${record.id}`} />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <Table
      dataSource={services}
      columns={columns}
      rowKey="id"
      loading={loading}
      pagination={{ pageSize: 20 }}
      data-testid="service-table"
      onRow={(record) => ({
        onClick: () => navigate(`/services/${record.id}`),
        style: { cursor: 'pointer' },
        'data-testid': `service-row-${record.id}`,
      })}
    />
  );
};
