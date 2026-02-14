import styles from './status-dot.module.css';

export type StatusDotVariant = 'running' | 'stopped' | 'error';

interface StatusDotProps {
  status: StatusDotVariant;
}

export const StatusDot: React.FC<StatusDotProps> = ({ status }) => (
  <span
    className={`${styles.dot} ${styles[status]}`}
    data-testid="status-dot"
    data-status={status}
  />
);
