import React from 'react';
import { useTranslation } from 'react-i18next';
import { Descriptions, Tag, Button, Space, Popconfirm, Typography, Card } from 'antd';
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

const { Text } = Typography;

export const ProjectOverview: React.FC<ProjectOverviewProps> = ({
  project, runtime, onStart, onStop, onReload,
}) => {
  const { t } = useTranslation();
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
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 24 }}>
        <Space size={12}>
          <Button
            type="primary"
            size="large"
            style={{
              background: 'var(--brand-gradient)',
              border: 'none',
              boxShadow: '0 4px 12px var(--brand-glow)',
              height: 40,
              padding: '0 24px',
              fontWeight: 600,
              borderRadius: 8
            }}
            icon={<PlayCircleOutlined />}
            disabled={!canStart}
            onClick={onStart}
            data-testid="start-btn"
          >
            {t('projects.overview.start_project')}
          </Button>
          <Popconfirm title={t('projects.overview.stop_confirm')} onConfirm={onStop} disabled={!canStop} okButtonProps={{ danger: true }}>
            <Button
              danger
              size="large"
              style={{
                height: 40,
                background: 'rgba(239, 68, 68, 0.1)',
                border: '1px solid rgba(239, 68, 68, 0.3)',
                backdropFilter: 'blur(4px)',
                borderRadius: 8,
                fontWeight: 600
              }}
              icon={<StopOutlined />}
              disabled={!canStop}
              data-testid="stop-btn"
            >
              {t('common.stop')}
            </Button>
          </Popconfirm>
          <Button
            icon={<ReloadOutlined />}
            size="large"
            style={{
              height: 40,
              background: 'rgba(255,255,255,0.03)',
              border: '1px solid var(--surface-border)',
              backdropFilter: 'blur(4px)',
              borderRadius: 8,
              color: 'var(--text-secondary)'
            }}
            onClick={onReload}
            data-testid="reload-btn"
          >
            {t('common.reload')}
          </Button>
        </Space>

        <div style={{ display: 'flex', alignItems: 'center', gap: 12, padding: '8px 16px', background: 'rgba(255,255,255,0.03)', borderRadius: 100, border: '1px solid var(--surface-border)' }}>
          <StatusDot status={dotStatus} />
          <Text strong style={{ fontSize: 14, textTransform: 'capitalize' }}>{t(`common.status_${status}`, status)}</Text>
        </div>
      </div>

      <Card className="glass-panel" bordered={false}>
        <Descriptions
          bordered
          column={2}
          size="small"
          labelStyle={{ background: 'rgba(255,255,255,0.02)', fontWeight: 600, width: 160 }}
          contentStyle={{ background: 'transparent' }}
        >
          <Descriptions.Item label={t('projects.overview.project_id')}>{project.id}</Descriptions.Item>
          <Descriptions.Item label={t('projects.overview.display_name')}>{project.name}</Descriptions.Item>
          <Descriptions.Item label={t('projects.overview.service_template')}>
            <Tag bordered={false} style={{ background: 'var(--primary-dim)', color: 'var(--brand-primary)' }}>{project.serviceId}</Tag>
          </Descriptions.Item>
          <Descriptions.Item label={t('projects.overview.enabled')}>{project.enabled ? <Tag color="success">{t('common.yes')}</Tag> : <Tag>{t('common.no')}</Tag>}</Descriptions.Item>
          <Descriptions.Item label={t('projects.overview.validation')}>{project.valid ? <Tag color="success">{t('projects.overview.pass')}</Tag> : <Tag color="error">{t('projects.overview.fail')}: {project.error}</Tag>}</Descriptions.Item>
          <Descriptions.Item label={t('projects.overview.schedule_strategy')}><Text code>{project.schedule.type}</Text></Descriptions.Item>
          {runtime && (
            <>
              <Descriptions.Item label={t('projects.overview.active_instances')}>{runtime.runningInstances}</Descriptions.Item>
              <Descriptions.Item label={t('projects.overview.consecutive_failures')}>{runtime.schedule.consecutiveFailures}</Descriptions.Item>
            </>
          )}
        </Descriptions>
      </Card>
    </div>
  );
};
