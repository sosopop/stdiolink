import React from 'react';
import { Button, Collapse, Form, Input, Space, Typography } from 'antd';
import { DeleteOutlined, PlusOutlined } from '@ant-design/icons';
import { useTranslation } from 'react-i18next';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '../FieldRenderer';

interface ObjectFieldProps {
  field: FieldMeta;
  value: Record<string, unknown>;
  onChange: (value: Record<string, unknown>) => void;
  errors?: Record<string, string>;
  basePath?: string;
}

export const ObjectField: React.FC<ObjectFieldProps> = ({ field, value, onChange, errors, basePath }) => {
  const { t } = useTranslation();
  const val = value ?? {};
  const currentPath = basePath ?? field.name;
  const dynamicEntriesEnabled = (field.additionalProperties ?? true) && (field.fields?.length ?? 0) === 0;
  const dynamicEntries = dynamicEntriesEnabled ? Object.entries(val) : [];

  const handleFieldChange = (name: string, fieldValue: unknown) => {
    onChange({ ...val, [name]: fieldValue });
  };

  const nextDynamicKey = () => {
    let index = 1;
    while (Object.prototype.hasOwnProperty.call(val, `key${index}`)) {
      index += 1;
    }
    return `key${index}`;
  };

  const handleAddEntry = () => {
    onChange({ ...val, [nextDynamicKey()]: '' });
  };

  const handleRemoveEntry = (entryKey: string) => {
    const next = { ...val };
    delete next[entryKey];
    onChange(next);
  };

  const handleEntryChange = (previousKey: string, nextKey: string, nextValue: unknown) => {
    const entries = Object.entries(val);
    const nextEntries = entries.map(([entryKey, entryValue]) => {
      if (entryKey !== previousKey) {
        return [entryKey, entryValue];
      }
      return [nextKey, nextValue];
    });

    if (!entries.some(([entryKey]) => entryKey === previousKey)) {
      nextEntries.push([nextKey, nextValue]);
    }

    onChange(Object.fromEntries(nextEntries));
  };

  const items = [
    {
      key: field.name,
      label: field.name,
      children: (
        <div data-testid={`object-fields-${field.name}`}>
          {(field.fields ?? []).map((child) => (
            <FieldRenderer
              key={child.name}
              field={child}
              value={val[child.name]}
              onChange={(v) => handleFieldChange(child.name, v)}
              error={errors?.[`${currentPath}.${child.name}`]}
              errors={errors}
              basePath={`${currentPath}.${child.name}`}
            />
          ))}

          {dynamicEntriesEnabled && (
            <div style={{ display: 'grid', gap: 2, marginTop: dynamicEntries.length > 0 ? 8 : 0 }}>
              {dynamicEntries.map(([entryKey, entryValue], index) => (
                <div
                  key={`${field.name}-${index}`}
                  data-testid={`object-entry-${field.name}-${index}`}
                  style={{
                    border: '1px solid var(--border-subtle)',
                    borderRadius: 8,
                    padding: 8,
                    background: 'var(--surface-layer1)',
                  }}
                >
                  <Space.Compact style={{ display: 'flex', width: '100%' }}>
                    <Input
                      value={entryKey}
                      onChange={(e) => handleEntryChange(entryKey, e.target.value, entryValue ?? '')}
                      data-testid={`object-entry-key-${field.name}-${index}`}
                    />
                    <Input
                      value={String(entryValue ?? '')}
                      onChange={(e) => handleEntryChange(entryKey, entryKey, e.target.value)}
                      data-testid={`object-entry-value-${field.name}-${index}`}
                    />
                    <Button
                      type="text"
                      danger
                      icon={<DeleteOutlined />}
                      onClick={() => handleRemoveEntry(entryKey)}
                      data-testid={`remove-entry-${field.name}-${index}`}
                    />
                  </Space.Compact>
                </div>
              ))}

              {dynamicEntries.length === 0 && (
                <Typography.Text type="secondary" data-testid={`object-empty-${field.name}`}>
                  {field.description}
                </Typography.Text>
              )}

              <Button
                type="dashed"
                icon={<PlusOutlined />}
                onClick={handleAddEntry}
                data-testid={`add-entry-${field.name}`}
              >
                {t('schema.add_item')}
              </Button>
            </div>
          )}
        </div>
      ),
    },
  ];

  return (
    <Form.Item data-testid={`field-${field.name}`}>
      <Collapse items={items} defaultActiveKey={[field.name]} size="small" />
    </Form.Item>
  );
};
