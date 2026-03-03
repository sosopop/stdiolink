import React from 'react';
import { Button, Form, Typography, Tag, Tooltip, Empty } from 'antd';
import { PlusOutlined, DeleteOutlined } from '@ant-design/icons';
import { useTranslation } from 'react-i18next';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '../FieldRenderer';
import { getDefaultItem } from '../utils/fieldDefaults';

const { Text } = Typography;

interface ArrayFieldProps {
  field: FieldMeta;
  value: unknown[];
  onChange: (value: unknown[]) => void;
  error?: string;
  errors?: Record<string, string>;
  basePath?: string;
}

export const ArrayField: React.FC<ArrayFieldProps> = ({ field, value, onChange, error, errors, basePath }) => {
  const { t } = useTranslation();
  const arr = value ?? [];
  const canAdd = field.maxItems === undefined || arr.length < field.maxItems;
  const canRemove = field.minItems === undefined || arr.length > field.minItems;
  const currentPath = basePath ?? field.name;
  const isObjectItem = field.items?.type === 'object';

  const handleAdd = () => {
    const subFields = isObjectItem ? (field.items?.fields ?? []) : [];
    if (subFields.length > 0) {
      const newItem: Record<string, unknown> = {};
      for (const subField of subFields) {
        if (subField.default !== undefined) {
          newItem[subField.name] = subField.default;
        } else if (subField.type === 'enum' && subField.enum?.[0] !== undefined) {
          newItem[subField.name] = subField.enum[0];
        } else {
          newItem[subField.name] = getDefaultItem(subField.type);
        }
      }
      onChange([...arr, newItem]);
      return;
    }
    onChange([...arr, getDefaultItem(field.items?.type)]);
  };

  const handleRemove = (index: number) => {
    onChange(arr.filter((_, i) => i !== index));
  };

  const handleItemChange = (index: number, val: unknown) => {
    const next = [...arr];
    next[index] = val;
    onChange(next);
  };

  // 尝试获取该项的摘要（显示第一个字段的值）
  const getItemSummary = (item: Record<string, unknown>) => {
    if (!isObjectItem || !item || typeof item !== 'object') return null;
    const firstKey = Object.keys(item)[0];
    if (!firstKey) return null;
    const val = item[firstKey];
    if (val !== undefined && val !== null && val !== '') {
      return (
        <Text type="secondary" style={{ fontSize: 12, marginLeft: 8 }} ellipsis>
          {`${firstKey}: ${val}`}
        </Text>
      );
    }
    return null;
  };

  return (
    <Form.Item
      label={
        <span style={{ fontWeight: 600, letterSpacing: '0.02em' }}>
          {field.name}
          {arr.length > 0 && (
            <Tag style={{ marginLeft: 8, borderRadius: 10, border: 'none', background: 'var(--surface-layer2)' }}>
              {arr.length}
            </Tag>
          )}
        </span>
      }
      required={field.required}
      help={error || field.description}
      validateStatus={error ? 'error' : undefined}
      data-testid={`field-${field.name}`}
      style={{ marginBottom: 24 }}
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        {arr.length === 0 ? (
          <div style={{
            padding: '24px',
            background: 'rgba(255,255,255,0.01)',
            borderRadius: '12px',
            border: '1px dashed var(--border-subtle)',
            textAlign: 'center'
          }}>
            <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={<Text type="secondary">{t('schema.no_items_yet', 'No items added')}</Text>} />
          </div>
        ) : (
          arr.map((item, i) => (
            <div
              key={i}
              data-testid={`array-item-${i}`}
              style={{
                background: 'var(--surface-layer1)',
                border: '1px solid var(--border-subtle)',
                borderRadius: '12px',
                padding: '16px',
                margin: '4px 4px 12px 4px', // 预留阴影空间，防止被裁剪
                position: 'relative',
                transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
                boxShadow: 'var(--shadow-card)'
              }}
            >
              {/* Item Header */}
              <div style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                marginBottom: isObjectItem ? 12 : 0,
                paddingBottom: isObjectItem ? 0 : 0,
                borderBottom: isObjectItem ? '1px solid var(--border-subtle)' : 'none'
              }}>
                <div style={{ display: 'flex', alignItems: 'center', flex: 1, minWidth: 0 }}>
                  <div style={{
                    width: 24,
                    height: 24,
                    borderRadius: '50%',
                    background: 'var(--primary-dim)',
                    color: 'var(--primary-color)',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    fontSize: 10,
                    fontWeight: 700,
                    marginRight: 8,
                    flexShrink: 0
                  }}>
                    {i + 1}
                  </div>
                  <Text strong style={{ fontSize: 13 }}>
                    {field.items?.name || t('schema.item', 'Item')}
                  </Text>
                  {getItemSummary(item as Record<string, unknown>)}
                </div>

                <div style={{ display: 'flex', gap: 4 }}>
                  <Tooltip title={t('common.delete')}>
                    <Button
                      type="text"
                      size="small"
                      danger
                      icon={<DeleteOutlined />}
                      disabled={!canRemove}
                      onClick={() => handleRemove(i)}
                      data-testid={`remove-item-${i}`}
                      style={{ borderRadius: 6 }}
                    />
                  </Tooltip>
                </div>
              </div>

              {/* Item Body */}
              <div style={{ marginLeft: isObjectItem ? 0 : 0 }}>
                {field.items ? (
                  <FieldRenderer
                    field={{ ...field.items, name: `${field.name}[${i}]` }}
                    value={item}
                    onChange={(v) => handleItemChange(i, v)}
                    error={errors?.[`${currentPath}[${i}]`]}
                    errors={errors}
                    basePath={`${currentPath}[${i}]`}
                  />
                ) : (
                  <div style={{ padding: '8px 12px', background: 'var(--surface-layer2)', borderRadius: 8 }}>
                    <Text code>{JSON.stringify(item)}</Text>
                  </div>
                )}
              </div>
            </div>
          ))
        )}

        <Button
          type="dashed"
          icon={<PlusOutlined />}
          onClick={handleAdd}
          disabled={!canAdd}
          data-testid={`add-item-${field.name}`}
          block
          style={{
            height: 40,
            borderRadius: 12,
            borderWidth: 1,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            background: 'transparent',
            marginTop: 4
          }}
        >
          {t('schema.add_item')}
        </Button>
      </div>
    </Form.Item>
  );
};
