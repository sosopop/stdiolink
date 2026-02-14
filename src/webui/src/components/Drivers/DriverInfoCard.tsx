import React from 'react';
import { useTranslation } from 'react-i18next';
import { Descriptions } from 'antd';
import type { DriverMetaInfo } from '@/types/driver';

interface DriverInfoCardProps {
  info: DriverMetaInfo;
}

export const DriverInfoCard: React.FC<DriverInfoCardProps> = ({ info }) => {
  const { t } = useTranslation();
  return (
    <Descriptions bordered size="small" column={2} data-testid="driver-info-card">
      <Descriptions.Item label={t('drivers.info.id')}>{info.id || '--'}</Descriptions.Item>
      <Descriptions.Item label={t('drivers.info.name')}>{info.name}</Descriptions.Item>
      <Descriptions.Item label={t('drivers.info.version')}>{info.version}</Descriptions.Item>
      <Descriptions.Item label={t('drivers.info.vendor')}>{info.vendor || '--'}</Descriptions.Item>
      <Descriptions.Item label={t('drivers.info.description')} span={2}>{info.description || '--'}</Descriptions.Item>
      {info.capabilities && info.capabilities.length > 0 && (
        <Descriptions.Item label={t('drivers.info.capabilities')} span={2}>{info.capabilities.join(', ')}</Descriptions.Item>
      )}
      {info.profiles && info.profiles.length > 0 && (
        <Descriptions.Item label={t('drivers.info.profiles')} span={2}>{info.profiles.join(', ')}</Descriptions.Item>
      )}
    </Descriptions>
  );
};
