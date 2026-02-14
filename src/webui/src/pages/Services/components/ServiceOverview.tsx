import React from 'react';
import { Descriptions, Tag } from 'antd';
import type { ServiceDetail } from '@/types/service';

interface ServiceOverviewProps {
  service: ServiceDetail;
}

export const ServiceOverview: React.FC<ServiceOverviewProps> = ({ service }) => {
  return (
    <div data-testid="service-overview">
      <Descriptions bordered column={1} size="small">
        <Descriptions.Item label="ID">{service.id}</Descriptions.Item>
        <Descriptions.Item label="Name">{service.name}</Descriptions.Item>
        <Descriptions.Item label="Version"><Tag>{service.version}</Tag></Descriptions.Item>
        <Descriptions.Item label="Description">{service.manifest.description ?? '—'}</Descriptions.Item>
        <Descriptions.Item label="Author">{service.manifest.author ?? '—'}</Descriptions.Item>
        <Descriptions.Item label="Directory">{service.serviceDir}</Descriptions.Item>
        <Descriptions.Item label="Has Schema">{service.hasSchema ? 'Yes' : 'No'}</Descriptions.Item>
        <Descriptions.Item label="Projects">{service.projectCount}</Descriptions.Item>
      </Descriptions>
    </div>
  );
};
