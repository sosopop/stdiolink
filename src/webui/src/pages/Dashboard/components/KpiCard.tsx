import { Card } from 'antd';
import styles from '../dashboard.module.css';

interface KpiCardProps {
  title: string;
  value: number;
  icon: React.ReactNode;
  status?: 'normal' | 'warning' | 'error';
  subtitle?: string;
}

const statusColorMap: Record<string, string> = {
  normal: 'var(--color-success)',
  warning: 'var(--color-warning)',
  error: 'var(--color-error)',
};

export const KpiCard: React.FC<KpiCardProps> = ({ title, value, icon, status = 'normal', subtitle }) => (
  <Card className={`${styles.kpiCard} glass-panel hover-card`} bordered={false}>
    <div className={styles.kpiIcon} style={{ color: statusColorMap[status] }}>
      {icon}
    </div>
    <div className={styles.kpiValue} data-testid="kpi-value">
      {value}
    </div>
    <div className={styles.kpiTitle}>{title}</div>
    {subtitle && <div className={styles.kpiSubtitle}>{subtitle}</div>}
  </Card>
);
