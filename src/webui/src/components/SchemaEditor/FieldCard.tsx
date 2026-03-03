import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Tag, Button, Space, Typography, Tooltip } from 'antd';
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
  const isArrayObject = desc.type === 'array' && desc.items?.type === 'object';
  const isContainer = desc.type === 'object' || isArrayObject;
  const hasChildren = isContainer && field.children && field.children.length > 0;

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

  // 层级深度颜色计算
  const depthOpacity = 0.03 * (level + 1);
  const containerBorderColor = isArrayObject ? 'rgba(24, 144, 255, 0.3)' : (desc.type === 'object' ? 'rgba(146, 84, 222, 0.3)' : 'var(--border-subtle)');

  return (
    <div
      style={{
        marginLeft: level > 0 ? 24 : 0,
        marginBottom: 8,
        position: 'relative',
        transition: 'all 0.3s'
      }}
      data-testid={`field-card-${path}`}
    >
      {/* 垂直层级引导线 */}
      {level > 0 && (
        <div style={{
          position: 'absolute',
          left: -12,
          top: -8,
          bottom: isLast ? 24 : -8,
          width: 1,
          background: 'rgba(255,255,255,0.08)',
          borderRadius: 1
        }} />
      )}
      {level > 0 && (
        <div style={{
          position: 'absolute',
          left: -12,
          top: 16,
          width: 12,
          height: 1,
          background: 'rgba(255,255,255,0.08)',
          borderRadius: 1
        }} />
      )}

      <div
        style={{
          background: `rgba(255, 255, 255, ${depthOpacity})`,
          border: '1px solid var(--border-subtle)',
          borderLeft: `3px solid ${containerBorderColor}`,
          borderRadius: '8px',
          padding: '10px 16px',
          boxShadow: level === 0 ? 'var(--shadow-card)' : 'none',
          transition: 'all 0.2s',
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          backdropFilter: 'blur(4px)'
        }}
        className="field-card-container"
      >
        <div style={{ flex: 1, minWidth: 0, display: 'flex', alignItems: 'center', gap: 12 }}>
          {isContainer ? (
            <Button
              type="text"
              size="small"
              icon={expanded ? <DownOutlined style={{ fontSize: 10 }} /> : <RightOutlined style={{ fontSize: 10 }} />}
              onClick={() => setExpanded(!expanded)}
              data-testid={`field-toggle-${path}`}
              style={{
                width: 20,
                height: 20,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                background: 'rgba(255,255,255,0.03)',
                borderRadius: 4
              }}
            />
          ) : (
            <div style={{ width: 20 }} />
          )}

          <div style={{ display: 'flex', flexDirection: 'column', flex: 1, minWidth: 0 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Text strong style={{ fontSize: 14, letterSpacing: '0.01em' }}>{field.name}</Text>
              <Tag
                bordered={false}
                style={{
                  fontSize: 10,
                  height: 18,
                  lineHeight: '18px',
                  background: isContainer ? 'var(--primary-dim)' : 'var(--surface-layer2)',
                  color: isContainer ? 'var(--primary-color)' : 'inherit',
                  borderRadius: 4,
                  textTransform: 'uppercase',
                  fontWeight: 600
                }}
              >
                {desc.type ?? 'any'}
              </Tag>
              {desc.required && (
                <Tag
                  color="error"
                  bordered={false}
                  style={{ fontSize: 10, height: 18, lineHeight: '18px', borderRadius: 4, fontWeight: 600 }}
                >
                  {t('schema.required').toUpperCase()}
                </Tag>
              )}
            </div>

            {(desc.description || desc.default !== undefined || cs) && (
              <div style={{ marginTop: 4, display: 'flex', gap: 12, flexWrap: 'wrap' }}>
                {desc.description && (
                  <Text type="secondary" style={{ fontSize: 12 }} ellipsis title={desc.description}>
                    {desc.description}
                  </Text>
                )}
                {desc.default !== undefined && (
                  <Text type="secondary" style={{ fontSize: 11, fontStyle: 'italic', opacity: 0.8 }}>
                    {t('schema.default_prefix')}{JSON.stringify(desc.default)}
                  </Text>
                )}
                {cs && (
                  <Text type="secondary" style={{ fontSize: 11, color: 'var(--primary-color)', opacity: 0.7 }}>
                    {cs}
                  </Text>
                )}
              </div>
            )}
            {hasChildren && !expanded && (
              <div style={{ marginTop: 2 }}>
                <Text type="secondary" style={{ fontSize: 11, opacity: 0.6 }}>
                  {t('schema.child_fields', { count: field.children!.length })}
                </Text>
              </div>
            )}
          </div>
        </div>

        <Space size={2}>
          <Tooltip title={t('common.move_up')}>
            <Button
              type="text"
              size="small"
              icon={<ArrowUpOutlined style={{ fontSize: 12 }} />}
              disabled={isFirst}
              onClick={() => onMove(path, 'up')}
              data-testid={`field-up-${path}`}
              style={{ borderRadius: 4 }}
            />
          </Tooltip>
          <Tooltip title={t('common.move_down')}>
            <Button
              type="text"
              size="small"
              icon={<ArrowDownOutlined style={{ fontSize: 12 }} />}
              disabled={isLast}
              onClick={() => onMove(path, 'down')}
              data-testid={`field-down-${path}`}
              style={{ borderRadius: 4 }}
            />
          </Tooltip>
          <div style={{ width: 1, height: 16, background: 'rgba(255,255,255,0.06)', margin: '0 4px' }} />
          <Tooltip title={t('common.edit')}>
            <Button
              type="text"
              size="small"
              icon={<EditOutlined style={{ fontSize: 12 }} />}
              onClick={() => onEdit(path)}
              data-testid={`field-edit-${path}`}
              style={{ borderRadius: 4 }}
            />
          </Tooltip>
          <Tooltip title={t('common.delete')}>
            <Button
              type="text"
              size="small"
              danger
              icon={<DeleteOutlined style={{ fontSize: 12 }} />}
              onClick={() => onDelete(path)}
              data-testid={`field-delete-${path}`}
              style={{ borderRadius: 4 }}
            />
          </Tooltip>
        </Space>
      </div>

      {isContainer && expanded && (
        <div
          data-testid={`field-children-${path}`}
          style={{
            marginTop: 4,
            paddingTop: 4,
            position: 'relative'
          }}
        >
          {isArrayObject && (
            <div style={{ marginLeft: 36, marginBottom: 8, marginTop: 4 }}>
              <Tag
                color="blue"
                bordered={false}
                style={{
                  fontSize: 10,
                  fontWeight: 600,
                  borderRadius: 4,
                  background: 'rgba(24, 144, 255, 0.1)',
                  color: '#1890ff'
                }}
              >
                <PlusOutlined style={{ marginRight: 4 }} />
                {t('schema.array_item_fields').toUpperCase()}
              </Tag>
            </div>
          )}
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
            <div style={{ marginLeft: 36, marginTop: 4 }}>
              <Button
                type="dashed"
                size="small"
                icon={<PlusOutlined />}
                onClick={() => onAddChild(path)}
                style={{
                  borderRadius: 6,
                  fontSize: 12,
                  height: 28,
                  background: 'transparent',
                  borderColor: 'rgba(255,255,255,0.1)',
                  color: 'var(--primary-color)',
                  display: 'flex',
                  alignItems: 'center'
                }}
                data-testid={`field-add-child-${path}`}
              >
                {t('schema.add_child_field')}
              </Button>
            </div>
          )}
        </div>
      )}
    </div>
  );
};
