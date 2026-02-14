import React, { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useParams } from 'react-router-dom';
import { Tabs, Spin, Alert, Empty } from 'antd';
import { useServicesStore } from '@/stores/useServicesStore';
import { ServiceOverview } from './components/ServiceOverview';
import { ServiceFiles } from './components/ServiceFiles';
import { SchemaEditor } from '@/components/SchemaEditor/SchemaEditor';
import { ServiceProjects } from './components/ServiceProjects';

export const ServiceDetailPage: React.FC = () => {
  const { t } = useTranslation();
  const { id } = useParams<{ id: string }>();
  const { currentService, loading, error, fetchServiceDetail } = useServicesStore();

  useEffect(() => {
    if (id) fetchServiceDetail(id);
  }, [id, fetchServiceDetail]);

  if (loading) return <Spin data-testid="detail-loading" />;
  if (error) return <Alert type="error" message={error} data-testid="detail-error" />;
  if (!currentService) return <Empty description={t('services.detail.not_found')} data-testid="detail-not-found" />;

  const items = [
    {
      key: 'overview',
      label: t('services.detail.overview'),
      children: <ServiceOverview service={currentService} />,
    },
    {
      key: 'files',
      label: t('services.detail.files'),
      children: <ServiceFiles serviceId={currentService.id} />,
    },
    {
      key: 'schema',
      label: t('services.detail.schema'),
      children: <SchemaEditor serviceId={currentService.id} />,
    },
    {
      key: 'projects',
      label: t('services.detail.projects'),
      children: <ServiceProjects serviceId={currentService.id} />,
    },
  ];

  return (
    <div data-testid="page-service-detail">
      <h2 style={{ marginBottom: 16 }}>{currentService.name}</h2>
      <Tabs items={items} data-testid="detail-tabs" />
    </div>
  );
};

export default ServiceDetailPage;
