import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useParams, useNavigate } from 'react-router-dom';
import { Tabs, Button, Spin, Alert, Empty, Space } from 'antd';
import { ArrowLeftOutlined, PlayCircleOutlined } from '@ant-design/icons';
import { useDriversStore } from '@/stores/useDriversStore';
import { DriverInfoCard } from '@/components/Drivers/DriverInfoCard';
import { CommandCard } from '@/components/Drivers/CommandCard';
import { DriverDocs } from '@/components/Drivers/DriverDocs';
import { DocExportButton } from '@/components/Drivers/DocExportButton';
import { ExportMetaButton } from '@/components/Drivers/ExportMetaButton';

export const DriverDetailPage: React.FC = () => {
  const { t } = useTranslation();
  const { id } = useParams<{ id: string }>();
  const navigate = useNavigate();
  const { currentDriver, loading, error, fetchDriverDetail } = useDriversStore();
  const [activeTab, setActiveTab] = useState('metadata');

  useEffect(() => {
    if (id) fetchDriverDetail(id);
  }, [id, fetchDriverDetail]);

  const handleTestCommand = (commandName: string) => {
    navigate(`/driverlab?driverId=${id}&command=${commandName}`);
  };

  if (loading && !currentDriver) {
    return <Spin data-testid="detail-loading" />;
  }

  if (error && !currentDriver) {
    return <Alert type="error" message={error} data-testid="detail-error" />;
  }

  if (!currentDriver) {
    return <Empty description={t('drivers.detail.not_found')} data-testid="detail-not-found" />;
  }

  const meta = currentDriver.meta;

  const items = [
    {
      key: 'metadata',
      label: t('drivers.detail.metadata'),
      children: (
        <div data-testid="driver-metadata">
          {meta?.info ? (
            <DriverInfoCard info={meta.info} />
          ) : (
            <Alert type="info" message={t('drivers.detail.no_metadata')} data-testid="no-meta" />
          )}
        </div>
      ),
    },
    {
      key: 'commands',
      label: t('drivers.detail.commands'),
      children: (
        <div data-testid="driver-commands">
          {meta?.commands && meta.commands.length > 0 ? (
            meta.commands.map((cmd) => (
              <CommandCard key={cmd.name} command={cmd} driverId={currentDriver.id} onTest={handleTestCommand} />
            ))
          ) : (
            <Empty description={t('drivers.detail.no_commands')} data-testid="empty-commands" />
          )}
        </div>
      ),
    },
    {
      key: 'docs',
      label: t('drivers.detail.docs'),
      children: id ? <DriverDocs driverId={id} /> : null,
    },
  ];

  return (
    <div data-testid="page-driver-detail">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <Space>
          <Button icon={<ArrowLeftOutlined />} onClick={() => navigate('/drivers')} data-testid="back-btn" />
          <h2 style={{ margin: 0 }}>{meta?.info?.name || currentDriver.id}</h2>
        </Space>
        <Space>
          <Button icon={<PlayCircleOutlined />} onClick={() => navigate(`/driverlab?driverId=${id}`)} data-testid="test-in-lab-btn">
            {t('drivers.detail.test_in_lab')}
          </Button>
          <DocExportButton driverId={currentDriver.id} />
          <ExportMetaButton driverId={currentDriver.id} meta={meta} />
        </Space>
      </div>
      <Tabs activeKey={activeTab} onChange={setActiveTab} items={items} />
    </div>
  );
};

export default DriverDetailPage;
