import { Table, Button, Popconfirm } from 'antd';
import { StopOutlined } from '@ant-design/icons';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Instance } from '@/types/instance';

interface ActiveInstancesProps {
  instances: Instance[];
  onTerminate: (id: string) => void;
}

export const ActiveInstances: React.FC<ActiveInstancesProps> = ({ instances, onTerminate }) => {
  if (instances.length === 0) {
    return (
      <div className="glass-panel" style={{ padding: 24 }} data-testid="active-instances">
        <h3 style={{ marginBottom: 16 }}>Active Instances</h3>
        <div data-testid="empty-instances">No running instances</div>
      </div>
    );
  }

  const columns = [
    {
      title: 'Status',
      dataIndex: 'status',
      key: 'status',
      width: 60,
      render: (status: string) => (
        <StatusDot status={status === 'running' ? 'running' : status === 'error' ? 'error' : 'stopped'} />
      ),
    },
    { title: 'Project', dataIndex: 'projectId', key: 'projectId' },
    { title: 'PID', dataIndex: 'pid', key: 'pid', width: 80 },
    {
      title: '',
      key: 'actions',
      width: 60,
      render: (_: unknown, record: Instance) => (
        <Popconfirm title="Terminate this instance?" onConfirm={() => onTerminate(record.id)}>
          <Button type="text" size="small" icon={<StopOutlined />} danger data-testid="terminate-btn" />
        </Popconfirm>
      ),
    },
  ];

  return (
    <div className="glass-panel" style={{ padding: 24 }} data-testid="active-instances">
      <h3 style={{ marginBottom: 16 }}>Active Instances</h3>
      <Table
        dataSource={instances}
        columns={columns}
        rowKey="id"
        size="small"
        pagination={false}
      />
    </div>
  );
};
