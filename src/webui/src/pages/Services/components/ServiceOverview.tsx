import React from 'react';
import { useTranslation } from 'react-i18next';
import { Descriptions, Tag } from 'antd';
import type { ServiceDetail } from '@/types/service';

interface ServiceOverviewProps {
  service: ServiceDetail;
}

export const ServiceOverview: React.FC<ServiceOverviewProps> = ({ service }) => {
  const { t } = useTranslation();
  return (
    <div data-testid="service-overview">
      <Descriptions bordered column={1} size="small">
        <Descriptions.Item label={t('services.overview.id')}>{service.id}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.name')}>{service.name}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.version')}><Tag>{service.version}</Tag></Descriptions.Item>
        <Descriptions.Item label={t('services.overview.description')}>{service.manifest.description ?? '—'}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.author')}>{service.manifest.author ?? '—'}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.directory')}>{service.serviceDir}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.has_schema')}>{service.hasSchema ? t('common.yes') : t('common.no')}</Descriptions.Item>
        <Descriptions.Item label={t('services.overview.projects')}>{service.projectCount}</Descriptions.Item>
      </Descriptions>
    </div>
  );
};
