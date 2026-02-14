import { useTranslation } from 'react-i18next';
import { Tag } from 'antd';
import type { ServerStatus } from '@/types/server';

interface ServerIndicatorProps {
  status: ServerStatus | null;
  connected: boolean;
}

export const ServerIndicator: React.FC<ServerIndicatorProps> = ({ status, connected }) => {
  const { t } = useTranslation();
  return (
  <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 16 }} data-testid="server-indicator">
    <Tag color={connected ? 'green' : 'red'}>{connected ? t('driverlab.status.connected') : t('driverlab.status.disconnected')}</Tag>
    {status && (
      <>
        <span>v{status.version}</span>
        <span>{status.host}:{status.port}</span>
      </>
    )}
  </div>
  );
};
