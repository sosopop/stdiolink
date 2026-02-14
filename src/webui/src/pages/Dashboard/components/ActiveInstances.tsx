import React from 'react';
import { Table, Button, Popconfirm, Typography } from 'antd';
import { StopOutlined } from '@ant-design/icons';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Instance } from '@/types/instance';
import styles from '../dashboard.module.css';

interface ActiveInstancesProps {
  instances: Instance[];
  onTerminate: (id: string) => void;
}

const { Text } = Typography;

export const ActiveInstances: React.FC<ActiveInstancesProps> = ({ instances, onTerminate }) => {
  const columns = [
    {
      title: 'Status',
      dataIndex: 'status',
      key: 'status',
      width: 80,
      render: (status: string) => (
        <StatusDot status={status === 'running' ? 'running' : status === 'error' ? 'error' : 'stopped'} />
      ),
    },
    { 
      title: 'Project', 
      dataIndex: 'projectId', 
      key: 'projectId',
      render: (id: string) => <Text strong>{id}</Text>
    },
    { 
      title: 'PID', 
      dataIndex: 'pid', 
      key: 'pid', 
      width: 100,
      render: (pid: number) => <Text code style={{ fontSize: 12 }}>{pid}</Text>
    },
    {
      title: '',
      key: 'actions',
      width: 60,
      render: (_: unknown, record: Instance) => (
        <Popconfirm 
          title="Terminate this instance?" 
          onConfirm={() => onTerminate(record.id)}
          okText="Yes"
          cancelText="No"
        >
          <Button type="text" size="small" icon={<StopOutlined />} danger data-testid="terminate-btn" />
        </Popconfirm>
      ),
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: 24 }} data-testid="active-instances">
      <h3 className={styles.sectionTitle}>Active Instances</h3>
      <Table
        dataSource={instances}
        columns={columns}
        rowKey="id"
        size="small"
        pagination={false}
        locale={{ emptyText: <div data-testid="empty-instances" style={{ padding: 20, color: 'var(--text-tertiary)' }}>No running instances</div> }}
      />
    </div>
  );
};
