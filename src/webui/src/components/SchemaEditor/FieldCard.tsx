import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, Tag, Button, Space, Typography } from 'antd';
import {
  EditOutlined,
  DeleteOutlined,
  ArrowUpOutlined,
  ArrowDownOutlined,
  PlusOutlined,
  RightOutlined,
  DownOutlined,
} from '@ant-design/icons';
import type { SchemaNode } from '@/utils/schemaPath';

const { Text } = Typography;

const MAX_DEPTH = 5;

interface FieldCardProps {
  field: SchemaNode;
  path: string;
  level: number;
  isFirst: boolean;
  isLast: boolean;
  onEdit: (path: string) => void;
  onDelete: (path: string) => void;
  onMove: (path: string, direction: 'up' | 'down') => void;
  onAddChild: (parentPath: string) => void;
}

export const FieldCard: React.FC<FieldCardProps> = ({
  field,
  path,
  level,
  isFirst,
  isLast,
  onEdit,
  onDelete,
  onMove,
  onAddChild,
}) => {
  const { t } = useTranslation();
  const [expanded, setExpanded] = useState(true);
  const desc = field.descriptor;
  const hasChildren = desc.type === 'object' && field.children && field.children.length > 0;
  const isObject = desc.type === 'object';

  const constraintSummary = () => {
    const parts: string[] = [];
    const c = desc.constraints;
    if (!c) return null;
    if (c.min !== undefined) parts.push(`min=${c.min}`);
    if (c.max !== undefined) parts.push(`max=${c.max}`);
    if (c.minLength !== undefined) parts.push(`minLength=${c.minLength}`);
    if (c.maxLength !== undefined) parts.push(`maxLength=${c.maxLength}`);
    if (c.pattern) parts.push(`pattern=${c.pattern}`);
    if (c.minItems !== undefined) parts.push(`minItems=${c.minItems}`);
    if (c.maxItems !== undefined) parts.push(`maxItems=${c.maxItems}`);
    if (c.enumValues) parts.push(`values=[${(c.enumValues as string[]).join(', ')}]`);
    return parts.length > 0 ? parts.join(', ') : null;
  };

  const cs = constraintSummary();

  return (
    <div style={{ marginLeft: level * 24, marginBottom: 8 }} data-testid={`field-card-${path}`}>
      <Card size="small" style={{ background: `rgba(255,255,255,${0.02 * (level + 1)})` }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
          <div style={{ flex: 1 }}>
            <Space>
              {isObject && (
                <Button
                  type="text"
                  size="small"
                  icon={expanded ? <DownOutlined /> : <RightOutlined />}
                  onClick={() => setExpanded(!expanded)}
                  data-testid={`field-toggle-${path}`}
                />
              )}
              <Text strong>{field.name}</Text>
              <Tag>{desc.type ?? 'any'}</Tag>
              {desc.required && <Tag color="red">{t('schema.required')}</Tag>}
            </Space>
            {desc.description && (
              <div><Text type="secondary" style={{ fontSize: 12 }}>{desc.description}</Text></div>
            )}
            {desc.default !== undefined && (
              <div><Text type="secondary" style={{ fontSize: 12 }}>{t('schema.default_prefix')}{JSON.stringify(desc.default)}</Text></div>
            )}
            {cs && (
              <div><Text type="secondary" style={{ fontSize: 12 }}>{t('schema.constraints_prefix')}{cs}</Text></div>
            )}
            {hasChildren && !expanded && (
              <div><Text type="secondary" style={{ fontSize: 12 }}>{t('schema.child_fields', { count: field.children!.length })}</Text></div>
            )}
          </div>
          <Space size={4}>
            <Button
              type="text"
              size="small"
              icon={<ArrowUpOutlined />}
              disabled={isFirst}
              onClick={() => onMove(path, 'up')}
              data-testid={`field-up-${path}`}
            />
            <Button
              type="text"
              size="small"
              icon={<ArrowDownOutlined />}
              disabled={isLast}
              onClick={() => onMove(path, 'down')}
              data-testid={`field-down-${path}`}
            />
            <Button
              type="text"
              size="small"
              icon={<EditOutlined />}
              onClick={() => onEdit(path)}
              data-testid={`field-edit-${path}`}
            />
            <Button
              type="text"
              size="small"
              danger
              icon={<DeleteOutlined />}
              onClick={() => onDelete(path)}
              data-testid={`field-delete-${path}`}
            />
          </Space>
        </div>
      </Card>

      {isObject && expanded && (
        <div data-testid={`field-children-${path}`}>
          {field.children?.map((child, idx) => (
            <FieldCard
              key={child.name}
              field={child}
              path={`${path}.${child.name}`}
              level={level + 1}
              isFirst={idx === 0}
              isLast={idx === (field.children!.length - 1)}
              onEdit={onEdit}
              onDelete={onDelete}
              onMove={onMove}
              onAddChild={onAddChild}
            />
          ))}
          {level < MAX_DEPTH - 1 && (
            <Button
              type="dashed"
              size="small"
              icon={<PlusOutlined />}
              onClick={() => onAddChild(path)}
              style={{ marginLeft: (level + 1) * 24, marginTop: 4 }}
              data-testid={`field-add-child-${path}`}
            >
              {t('schema.add_child_field')}
            </Button>
          )}
        </div>
      )}
    </div>
  );
};
