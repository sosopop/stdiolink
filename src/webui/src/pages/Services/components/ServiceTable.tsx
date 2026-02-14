import React from 'react';
import { Table, Button, Space, Popconfirm, Tag, Typography } from 'antd';
import { EyeOutlined, DeleteOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import type { ServiceInfo } from '@/types/service';

interface ServiceTableProps {
  services: ServiceInfo[];
  loading?: boolean;
  onDelete: (id: string) => void;
}

const { Text } = Typography;

export const ServiceTable: React.FC<ServiceTableProps> = ({ services, loading, onDelete }) => {
  const navigate = useNavigate();

  const columns = [
    {
      title: 'Service ID',
      dataIndex: 'id',
      key: 'id',
      render: (id: string) => (
        <Text strong style={{ color: 'var(--brand-primary)', cursor: 'pointer' }} onClick={() => navigate(`/services/${id}`)}>
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
      title: 'Version',
      dataIndex: 'version',
      key: 'version',
      render: (v: string) => (
        <Tag bordered={false} style={{ backgroundColor: 'rgba(99, 102, 241, 0.1)', color: 'var(--brand-primary)' }}>
          v{v}
        </Tag>
      ),
    },
    {
      title: 'Projects',
      dataIndex: 'projectCount',
      key: 'projectCount',
      align: 'center' as const,
      render: (count: number) => (
        <Text code style={{ fontSize: 13 }}>{count}</Text>
      )
    },
    {
      title: 'Actions',
      key: 'actions',
      align: 'right' as const,
      render: (_: unknown, record: ServiceInfo) => (
        <Space size={4}>
          <Button
            type="text"
            icon={<EyeOutlined />}
            data-testid={`view-${record.id}`}
            onClick={(e) => { e.stopPropagation(); navigate(`/services/${record.id}`); }}
          />
          <Popconfirm
            title={`Delete service "${record.id}"?`}
            description={record.projectCount > 0 ? `Warning: This service has ${record.projectCount} active projects.` : undefined}
            onConfirm={(e) => { e?.stopPropagation(); onDelete(record.id); }}
            onCancel={(e) => e?.stopPropagation()}
            okText="Delete"
            cancelText="Cancel"
            okButtonProps={{ danger: true }}
          >
            <Button 
              type="text" 
              danger 
              icon={<DeleteOutlined />} 
              data-testid={`delete-${record.id}`}
              onClick={(e) => e.stopPropagation()}
            />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: '8px 0' }}>
      <Table
        dataSource={services}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={{ 
          pageSize: 10,
          showSizeChanger: false,
          style: { marginRight: 24 }
        }}
        data-testid="service-table"
        onRow={(record) => ({
          onClick: () => navigate(`/services/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
          'data-testid': `service-row-${record.id}`,
        })}
      />
    </div>
  );
};
