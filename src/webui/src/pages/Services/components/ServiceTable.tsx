import React from 'react';
import { Table, Button, Space, Popconfirm, Tag, Typography } from 'antd';
import { EyeOutlined, DeleteOutlined, SettingOutlined, AppstoreOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import type { ServiceInfo } from '@/types/service';

interface ServiceTableProps {
  services: ServiceInfo[];
  loading: boolean;
  onDelete: (id: string) => void;
}

const { Text } = Typography;

export const ServiceTable: React.FC<ServiceTableProps> = ({ services, loading, onDelete }) => {
  const navigate = useNavigate();

  const columns = [
    {
      title: 'Service Template',
      key: 'name',
      render: (_: unknown, record: ServiceInfo) => (
        <Space size={12}>
          <div style={{ width: 36, height: 36, background: 'rgba(99, 102, 241, 0.1)', borderRadius: 10, display: 'grid', placeItems: 'center' }}>
            <AppstoreOutlined style={{ color: 'var(--brand-primary)', fontSize: 18 }} />
          </div>
          <Space direction="vertical" size={0}>
            <Text strong style={{ fontSize: 15 }}>{record.name}</Text>
            <Text type="secondary" style={{ fontSize: 11, fontFamily: 'var(--font-mono)' }}>{record.id}</Text>
          </Space>
        </Space>
      ),
    },
    {
      title: 'Version',
      dataIndex: 'version',
      key: 'version',
      width: 120,
      render: (v: string) => (
        <Tag bordered={false} style={{ background: 'rgba(255,255,255,0.05)', color: 'var(--text-secondary)', fontWeight: 600 }}>
          v{v}
        </Tag>
      ),
    },
    {
      title: 'Active Projects',
      dataIndex: 'projectCount',
      key: 'projectCount',
      width: 150,
      align: 'center' as const,
      render: (count: number) => (
        <div style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
          <Text strong style={{ fontSize: 16 }}>{count}</Text>
          <Text type="secondary" style={{ fontSize: 12 }}>instances</Text>
        </div>
      )
    },
    {
      title: 'Actions',
      key: 'actions',
      align: 'right' as const,
      width: 150,
      render: (_: unknown, record: ServiceInfo) => (
        <Space size={4}>
          <Button
            type="text"
            icon={<SettingOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/services/${record.id}/config`); }}
          />
          <Button
            type="text"
            icon={<EyeOutlined />}
            onClick={(e) => { e.stopPropagation(); navigate(`/services/${record.id}`); }}
          />
          <Popconfirm
            title="Delete service?"
            description={record.projectCount > 0 ? `This template is used by ${record.projectCount} projects.` : "Are you sure you want to delete this template?"}
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
              onClick={(e) => e.stopPropagation()}
            />
          </Popconfirm>
        </Space>
      ),
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: '0 0 16px 0', overflow: 'hidden' }}>
      <Table
        dataSource={services}
        columns={columns}
        rowKey="id"
        loading={loading}
        pagination={{
          pageSize: 10,
          style: { marginRight: 24, marginTop: 24 },
          showTotal: (total) => <Text type="secondary" style={{ fontSize: 12 }}>{total} templates available</Text>
        }}
        onRow={(record) => ({
          onClick: () => navigate(`/services/${record.id}`),
          className: 'hover-row',
          style: { cursor: 'pointer' },
        })}
      />
    </div>
  );
};
