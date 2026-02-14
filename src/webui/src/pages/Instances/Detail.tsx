import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useParams, useNavigate } from 'react-router-dom';
import { Tabs, Button, Spin, Alert, Empty, Descriptions, Popconfirm, Space, Card, Typography, Tag } from 'antd';
import { ArrowLeftOutlined, ReloadOutlined, StopOutlined } from '@ant-design/icons';
import { useInstancesStore } from '@/stores/useInstancesStore';
import { ProcessTree } from '@/components/Instances/ProcessTree';
import { ResourceChart } from '@/components/Instances/ResourceChart';
import { ResourceMetricCards } from '@/components/Instances/ResourceMetricCards';
import { LogViewer } from '@/components/LogViewer/LogViewer';
import { StatusDot } from '@/components/StatusDot/StatusDot';

const { Text, Title } = Typography;

export const InstanceDetailPage: React.FC = () => {
  const { t } = useTranslation();
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
    return (
      <div style={{ display: 'grid', placeItems: 'center', height: '60vh' }}>
        <Spin size="large" tip={t('instances.detail.loading')} data-testid="detail-loading" />
      </div>
    );
  }

  if (error && !currentInstance) {
    return <Alert type="error" message={error} showIcon style={{ borderRadius: 12 }} data-testid="detail-error" />;
  }

  if (!currentInstance) {
    return <Empty description={t('instances.detail.not_found')} data-testid="detail-not-found" />;
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
      label: t('instances.detail.overview'),
      children: (
        <div data-testid="instance-overview">
          <Card className="glass-panel" bordered={false} style={{ marginBottom: 24 }}>
            <Descriptions
              bordered
              size="small"
              column={2}
              labelStyle={{ background: 'rgba(255,255,255,0.02)', fontWeight: 600, width: 140 }}
            >
              <Descriptions.Item label={t('instances.detail.instance_id')}>{currentInstance.id}</Descriptions.Item>
              <Descriptions.Item label={t('instances.detail.project')}><Tag>{currentInstance.projectId}</Tag></Descriptions.Item>
              <Descriptions.Item label={t('projects.table.service_template')}><Tag>{currentInstance.serviceId}</Tag></Descriptions.Item>
              <Descriptions.Item label={t('common.status')}>
                <Space>
                  <StatusDot status={currentInstance.status === 'running' ? 'running' : 'stopped'} />
                  <Text strong>{currentInstance.status}</Text>
                </Space>
              </Descriptions.Item>
              <Descriptions.Item label={t('dashboard.table.system_pid')}><Text code>{currentInstance.pid}</Text></Descriptions.Item>
              <Descriptions.Item label={t('instances.detail.started_at')}>{new Date(currentInstance.startedAt).toLocaleString()}</Descriptions.Item>
              {currentInstance.workingDirectory && (
                <Descriptions.Item label={t('instances.detail.working_dir')} span={2}><Text type="secondary" style={{ fontSize: 12 }}>{currentInstance.workingDirectory}</Text></Descriptions.Item>
              )}
            </Descriptions>
          </Card>
          <ResourceMetricCards processes={resources} />
        </div>
      ),
    },
    {
      key: 'process-tree',
      label: t('instances.detail.process_tree'),
      children: (
        <div data-testid="instance-process-tree" className="glass-panel" style={{ padding: 24 }}>
          <ProcessTree
            tree={processTree?.tree ?? null}
            summary={processTree?.summary ?? null}
          />
        </div>
      ),
    },
    {
      key: 'resources',
      label: t('instances.detail.resources'),
      children: (
        <div data-testid="instance-resources">
          <ResourceMetricCards processes={resources} />
          <Card className="glass-panel" bordered={false} style={{ marginTop: 24, padding: 12 }}>
            <ResourceChart data={resourceHistory} />
          </Card>
        </div>
      ),
    },
    {
      key: 'logs',
      label: t('instances.detail.logs'),
      children: (
        <div data-testid="instance-logs">
          <div style={{ display: 'flex', justifyContent: 'flex-end', marginBottom: 12 }}>
            <Button
              icon={<ReloadOutlined />}
              onClick={() => id && fetchLogs(id)}
              data-testid="refresh-logs-btn"
            >
              {t('instances.detail.refresh_logs')}
            </Button>
          </div>
          <LogViewer lines={logs} />
        </div>
      ),
    },
  ];

  return (
    <div data-testid="page-instance-detail">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 32 }}>
        <Space size={16}>
          <Button
            type="text"
            icon={<ArrowLeftOutlined />}
            onClick={() => navigate('/instances')}
            data-testid="back-btn"
            style={{ color: 'var(--text-secondary)' }}
          />
          <div>
            <Title level={3} style={{ margin: 0 }}>{t('instances.detail.trace')}</Title>
            <Text type="secondary" code>{currentInstance.id}</Text>
          </div>
        </Space>
        <Popconfirm
          title={t('instances.detail.terminate_confirm')}
          onConfirm={handleTerminate}
          okText={t('common.terminate')}
          cancelText={t('common.cancel')}
          okButtonProps={{ danger: true }}
        >
          <Button danger size="large" icon={<StopOutlined />} data-testid="terminate-btn">
            {t('instances.detail.terminate_btn')}
          </Button>
        </Popconfirm>
      </div>
      <Tabs activeKey={activeTab} onChange={setActiveTab} items={items} />
    </div>
  );
};

export default InstanceDetailPage;
