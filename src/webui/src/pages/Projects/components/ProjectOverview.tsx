import React from 'react';
import { Descriptions, Tag, Button, Space, Popconfirm } from 'antd';
import { PlayCircleOutlined, StopOutlined, ReloadOutlined } from '@ant-design/icons';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Project, ProjectRuntime } from '@/types/project';

interface ProjectOverviewProps {
  project: Project;
  runtime: ProjectRuntime | null;
  onStart: () => void;
  onStop: () => void;
  onReload: () => void;
}

export const ProjectOverview: React.FC<ProjectOverviewProps> = ({
  project, runtime, onStart, onStop, onReload,
}) => {
  const status = runtime?.status ?? 'stopped';
  const canStart = project.enabled && project.valid && status !== 'running';
  const canStop = status === 'running';
  const dotStatus = status === 'running'
    ? 'running'
    : status === 'invalid'
      ? 'error'
      : 'stopped';

  return (
    <div data-testid="project-overview">
      <Space style={{ marginBottom: 16 }}>
        <Button
          type="primary"
          icon={<PlayCircleOutlined />}
          disabled={!canStart}
          onClick={onStart}
          data-testid="start-btn"
        >
          Start
        </Button>
        <Popconfirm title="Stop this project?" onConfirm={onStop} disabled={!canStop}>
          <Button danger icon={<StopOutlined />} disabled={!canStop} data-testid="stop-btn">
            Stop
          </Button>
        </Popconfirm>
        <Button icon={<ReloadOutlined />} onClick={onReload} data-testid="reload-btn">
          Reload
        </Button>
      </Space>

      <Descriptions bordered column={1} size="small">
        <Descriptions.Item label="ID">{project.id}</Descriptions.Item>
        <Descriptions.Item label="Name">{project.name}</Descriptions.Item>
        <Descriptions.Item label="Service"><Tag>{project.serviceId}</Tag></Descriptions.Item>
        <Descriptions.Item label="Status">
          <StatusDot status={dotStatus} />
          {' '}{status}
        </Descriptions.Item>
        <Descriptions.Item label="Enabled">{project.enabled ? 'Yes' : 'No'}</Descriptions.Item>
        <Descriptions.Item label="Valid">{project.valid ? 'Yes' : <Tag color="red">{project.error ?? 'Invalid'}</Tag>}</Descriptions.Item>
        <Descriptions.Item label="Schedule">{project.schedule.type}</Descriptions.Item>
        {runtime && (
          <>
            <Descriptions.Item label="Running Instances">{runtime.runningInstances}</Descriptions.Item>
            <Descriptions.Item label="Consecutive Failures">{runtime.schedule.consecutiveFailures}</Descriptions.Item>
          </>
        )}
      </Descriptions>
    </div>
  );
};
