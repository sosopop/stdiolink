import React, { useEffect, useState } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { Tabs, Button, Spin, Alert, Empty, Descriptions, Popconfirm, Space } from 'antd';
import { ArrowLeftOutlined, ReloadOutlined } from '@ant-design/icons';
import { useInstancesStore } from '@/stores/useInstancesStore';
import { ProcessTree } from '@/components/Instances/ProcessTree';
import { ResourceChart } from '@/components/Instances/ResourceChart';
import { ResourceMetricCards } from '@/components/Instances/ResourceMetricCards';
import { LogViewer } from '@/components/LogViewer/LogViewer';

export const InstanceDetailPage: React.FC = () => {
  const { id } = useParams<{ id: string }>();
  const navigate = useNavigate();
  const {
    currentInstance, processTree, resources, resourceHistory, logs,
    loading, error,
    fetchInstanceDetail, fetchProcessTree, fetchResources, fetchLogs, terminateInstance,
  } = useInstancesStore();
  const [activeTab, setActiveTab] = useState('overview');

  useEffect(() => {
    if (id) {
      fetchInstanceDetail(id);
      fetchProcessTree(id);
      fetchResources(id);
      fetchLogs(id);
    }
  }, [id, fetchInstanceDetail, fetchProcessTree, fetchResources, fetchLogs]);

  // 5s polling for active tabs
  useEffect(() => {
    if (!id) return;
    const interval = setInterval(() => {
      if (activeTab === 'overview' || activeTab === 'process-tree') {
        fetchProcessTree(id);
      }
      if (activeTab === 'overview' || activeTab === 'resources') {
        fetchResources(id);
      }
    }, 5000);
    return () => clearInterval(interval);
  }, [id, activeTab, fetchProcessTree, fetchResources]);

  if (loading && !currentInstance) {
    return <Spin data-testid="detail-loading" />;
  }

  if (error && !currentInstance) {
    return <Alert type="error" message={error} data-testid="detail-error" />;
  }

  if (!currentInstance) {
    return <Empty description="Instance not found" data-testid="detail-not-found" />;
  }

  const handleTerminate = async () => {
    if (id) {
      await terminateInstance(id);
      navigate('/instances');
    }
  };

  const items = [
    {
      key: 'overview',
      label: 'Overview',
      children: (
        <div data-testid="instance-overview">
          <Descriptions bordered size="small" column={2} style={{ marginBottom: 16 }}>
            <Descriptions.Item label="ID">{currentInstance.id}</Descriptions.Item>
            <Descriptions.Item label="Project">{currentInstance.projectId}</Descriptions.Item>
            <Descriptions.Item label="Service">{currentInstance.serviceId}</Descriptions.Item>
            <Descriptions.Item label="Status">{currentInstance.status}</Descriptions.Item>
            <Descriptions.Item label="PID">{currentInstance.pid}</Descriptions.Item>
            <Descriptions.Item label="Started At">{currentInstance.startedAt}</Descriptions.Item>
            {currentInstance.workingDirectory && (
              <Descriptions.Item label="Working Dir" span={2}>{currentInstance.workingDirectory}</Descriptions.Item>
            )}
          </Descriptions>
          <ResourceMetricCards processes={resources} />
        </div>
      ),
    },
    {
      key: 'process-tree',
      label: 'Process Tree',
      children: (
        <div data-testid="instance-process-tree">
          <ProcessTree
            tree={processTree?.tree ?? null}
            summary={processTree?.summary ?? null}
          />
        </div>
      ),
    },
    {
      key: 'resources',
      label: 'Resources',
      children: (
        <div data-testid="instance-resources">
          <ResourceMetricCards processes={resources} />
          <div style={{ marginTop: 16 }}>
            <ResourceChart data={resourceHistory} />
          </div>
        </div>
      ),
    },
    {
      key: 'logs',
      label: 'Logs',
      children: (
        <div data-testid="instance-logs">
          <div style={{ marginBottom: 8 }}>
            <Button
              icon={<ReloadOutlined />}
              size="small"
              onClick={() => id && fetchLogs(id)}
              data-testid="refresh-logs-btn"
            >
              Refresh
            </Button>
          </div>
          <LogViewer lines={logs} />
        </div>
      ),
    },
  ];

  return (
    <div data-testid="page-instance-detail">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <Space>
          <Button icon={<ArrowLeftOutlined />} onClick={() => navigate('/instances')} data-testid="back-btn" />
          <h2 style={{ margin: 0 }}>Instance: {currentInstance.id}</h2>
        </Space>
        <Popconfirm title="Terminate this instance?" onConfirm={handleTerminate}>
          <Button danger data-testid="terminate-btn">Terminate</Button>
        </Popconfirm>
      </div>
      <Tabs activeKey={activeTab} onChange={setActiveTab} items={items} />
    </div>
  );
};

export default InstanceDetailPage;
