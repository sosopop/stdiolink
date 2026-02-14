import {
  AppstoreOutlined,
  ProjectOutlined,
  RocketOutlined,
  ApiOutlined,
  ClockCircleOutlined,
} from '@ant-design/icons';
import { KpiCard } from './KpiCard';
import type { ServerStatus } from '@/types/server';
import styles from '../dashboard.module.css';

interface KpiCardsProps {
  status: ServerStatus | null;
}

function formatUptime(ms: number): string {
  const hours = Math.floor(ms / 3600000);
  const days = Math.floor(hours / 24);
  const h = hours % 24;
  if (days > 0) return `${days}d ${h}h`;
  const minutes = Math.floor((ms % 3600000) / 60000);
  return `${h}h ${minutes}m`;
}

export const KpiCards: React.FC<KpiCardsProps> = ({ status }) => {
  const counts = status?.counts;

  return (
    <div className={styles.kpiSection} data-testid="kpi-section">
      <KpiCard
        title="Projects"
        value={counts?.projects.total ?? 0}
        icon={<ProjectOutlined />}
        color="var(--brand-primary)"
        subtitle={counts ? `${counts.projects.enabled} enabled` : undefined}
      />
      <KpiCard
        title="Instances"
        value={counts?.instances.total ?? 0}
        icon={<RocketOutlined />}
        color="var(--color-success)"
        subtitle={counts ? `${counts.instances.running} running` : undefined}
      />
      <KpiCard
        title="Services"
        value={counts?.services ?? 0}
        icon={<AppstoreOutlined />}
        color="#38bdf8" // Sky blue
      />
      <KpiCard
        title="Drivers"
        value={counts?.drivers ?? 0}
        icon={<ApiOutlined />}
        color="#fbbf24" // Amber
      />
      <KpiCard
        title="Uptime"
        value={status ? Math.floor(status.uptimeMs / 3600000) : 0}
        icon={<ClockCircleOutlined />}
        color="#a78bfa" // Light purple
        subtitle={status ? formatUptime(status.uptimeMs) : undefined}
      />
    </div>
  );
};
