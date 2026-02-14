import React from 'react';
import { Descriptions } from 'antd';
import type { DriverMetaInfo } from '@/types/driver';

interface DriverInfoCardProps {
  info: DriverMetaInfo;
}

export const DriverInfoCard: React.FC<DriverInfoCardProps> = ({ info }) => (
  <Descriptions bordered size="small" column={2} data-testid="driver-info-card">
    <Descriptions.Item label="ID">{info.id || '--'}</Descriptions.Item>
    <Descriptions.Item label="Name">{info.name}</Descriptions.Item>
    <Descriptions.Item label="Version">{info.version}</Descriptions.Item>
    <Descriptions.Item label="Vendor">{info.vendor || '--'}</Descriptions.Item>
    <Descriptions.Item label="Description" span={2}>{info.description || '--'}</Descriptions.Item>
    {info.capabilities && info.capabilities.length > 0 && (
      <Descriptions.Item label="Capabilities" span={2}>{info.capabilities.join(', ')}</Descriptions.Item>
    )}
    {info.profiles && info.profiles.length > 0 && (
      <Descriptions.Item label="Profiles" span={2}>{info.profiles.join(', ')}</Descriptions.Item>
    )}
  </Descriptions>
);
