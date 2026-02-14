import React from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Button, Tooltip } from 'antd';
import { PlayCircleOutlined, StopOutlined, EyeOutlined, SettingOutlined } from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { StatusDot } from '@/components/StatusDot/StatusDot';
import type { Project, ProjectRuntime } from '@/types/project';
import styles from '../dashboard.module.css';

interface ProjectCardProps {
  project: Project;
  runtime?: ProjectRuntime;
  onStart: (id: string) => void;
  onStop: (id: string) => void;
}

export const ProjectCard: React.FC<ProjectCardProps> = ({ project, runtime, onStart, onStop }) => {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const status = runtime?.status || (project.valid ? 'stopped' : 'invalid');
  const dotStatus = status === 'running' ? 'running' : status === 'invalid' ? 'error' : 'stopped';
  const isRunning = status === 'running';
  const canStart = project.enabled && project.valid && !isRunning;

  return (
    <Card
      className={`${styles.projectCard} glass-panel hover-card`}
      bordered={false}
      onClick={() => navigate(`/projects/${project.id}`)}
    >
      <div className={styles.projectCardHeader}>
        <div className={styles.statusBadge}>
          <StatusDot status={dotStatus} size={8} />
        </div>
        <div className={styles.headerContent}>
          <div className={styles.projectTitle} title={project.name}>{project.name}</div>
          <div className={styles.projectId} title={project.id}>{project.id}</div>
        </div>
      </div>

      <div className={styles.projectCardBody}>
        <div className={styles.projectMetaGrid}>
          <div className={styles.metaItem}>
            <span className={styles.metaLabel}>{t('dashboard.project_card.instances')}</span>
            <span className={styles.metaValue}>{runtime?.runningInstances || 0}</span>
          </div>
          <div className={styles.metaItem}>
            <span className={styles.metaLabel}>{t('dashboard.project_card.strategy')}</span>
            <span className={styles.metaValue} style={{ textTransform: 'capitalize' }}>{project.schedule.type}</span>
          </div>
          <div className={styles.metaItem}>
            <span className={styles.metaLabel}>{t('dashboard.project_card.status')}</span>
            <span className={styles.metaValue} style={{
              color: isRunning ? 'var(--color-success)' : 'var(--text-tertiary)',
              fontWeight: 700
            }}>
              {t(`common.status_${status}`, status)}
            </span>
          </div>
          <div className={styles.metaItem}>
            <span className={styles.metaLabel}>{t('dashboard.project_card.service')}</span>
            <div className={styles.serviceTag} title={project.serviceId}>{project.serviceId}</div>
          </div>
        </div>
      </div>

      <div className={styles.projectCardActions} onClick={(e) => e.stopPropagation()}>
        <div className={styles.actionGroup}>
          {isRunning ? (
            <Tooltip title={t('common.stop')}>
              <Button
                type="text"
                danger
                icon={<StopOutlined />}
                onClick={() => onStop(project.id)}
                className={styles.actionBtn}
              />
            </Tooltip>
          ) : (
            <Tooltip title={t('common.start')}>
              <Button
                type="text"
                icon={<PlayCircleOutlined style={{ color: canStart ? 'var(--color-success)' : 'inherit', opacity: canStart ? 1 : 0.3 }} />}
                disabled={!canStart}
                onClick={() => onStart(project.id)}
                className={styles.actionBtn}
              />
            </Tooltip>
          )}
          <Tooltip title={t('common.configure')}>
            <Button
              type="text"
              icon={<SettingOutlined />}
              onClick={() => navigate(`/projects/${project.id}?tab=config`)}
              className={styles.actionBtn}
            />
          </Tooltip>
        </div>

        <Button
          type="link"
          size="small"
          className={styles.detailsBtn}
          icon={<EyeOutlined />}
          onClick={() => navigate(`/projects/${project.id}`)}
        >
          {t('common.details')}
        </Button>
      </div>
    </Card>
  );
};
