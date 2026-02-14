import React from 'react';
import { Tag } from 'antd';

interface ResourceBadgeProps {
  label: string;
  value: string;
  color?: string;
}

export const ResourceBadge: React.FC<ResourceBadgeProps> = ({ label, value, color }) => (
  <Tag data-testid={`resource-badge-${label.toLowerCase()}`} color={color} style={{ fontSize: 11 }}>
    {label}: {value}
  </Tag>
);
