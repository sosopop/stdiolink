import React from 'react';
import { Card } from 'antd';
import styles from '../dashboard.module.css';

interface KpiCardProps {
  title: string;
  value: number | string;
  icon: React.ReactNode;
  status?: 'normal' | 'warning' | 'error';
  subtitle?: string;
}

const statusColorMap: Record<string, string> = {
  normal: 'var(--color-success)',
  warning: 'var(--color-warning)',
  error: 'var(--color-error)',
  brand: 'var(--brand-primary)',
  info: 'var(--color-info)',
};

export const KpiCard: React.FC<KpiCardProps & { color?: string }> = ({ title, value, icon, status = 'normal', subtitle, color }) => {
  const iconColor = color || statusColorMap[status] || 'var(--text-secondary)';

  return (
    <Card
      className={`${styles.kpiCard} glass-panel hover-card`}
      bordered={false}
      style={{ borderBottom: `2px solid ${iconColor}40` }} // Subtle colored underline
    >
      <div className={styles.kpiHeader}>
        <span className={styles.kpiTitle}>{title}</span>
        <div className={styles.kpiIcon} style={{ color: iconColor, opacity: 1 }}>
          {icon}
        </div>
      </div>
      <div className={styles.kpiValue} style={{ color: iconColor }}>
        {value}
      </div>
      {subtitle && <div className={styles.kpiSubtitle}>{subtitle}</div>}
    </Card>
  );
};
