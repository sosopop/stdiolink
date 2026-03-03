import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Modal, Form, Input, Select, Switch, Collapse } from 'antd';
import type { SchemaNode, SchemaFieldDescriptor } from '@/utils/schemaPath';
import { ConstraintsSection } from './ConstraintsSection';
import { UiHintsSection } from './UiHintsSection';

const FIELD_TYPES = ['string', 'int', 'int64', 'double', 'bool', 'object', 'array', 'enum', 'any'] as const;
const ITEM_TYPES = FIELD_TYPES.filter((ft) => ft !== 'array');

function formatDefaultValue(value: unknown): string {
  if (value === undefined) return '';
  if (typeof value === 'string') return value;
  try {
    return JSON.stringify(value) ?? String(value);
  } catch {
    return String(value);
  }
}

interface FieldEditModalProps {
  visible: boolean;
  field: SchemaNode | null;
  existingNames: string[];
  onSave: (field: SchemaNode) => void;
  onCancel: () => void;
}

export const FieldEditModal: React.FC<FieldEditModalProps> = ({
  visible,
  field,
  existingNames,
  onSave,
  onCancel,
}) => {
  const { t } = useTranslation();
  const [name, setName] = useState('');
  const [type, setType] = useState<string>('string');
  const [required, setRequired] = useState(false);
  const [description, setDescription] = useState('');
  const [defaultValue, setDefaultValue] = useState('');
  const [defaultTouched, setDefaultTouched] = useState(false);
  const [itemsType, setItemsType] = useState<string>('string');
  const [constraints, setConstraints] = useState<Record<string, unknown>>({});
  const [uiHints, setUiHints] = useState<NonNullable<SchemaFieldDescriptor['ui']>>({});
  const [nameError, setNameError] = useState<string | null>(null);

  const isEdit = field !== null;

  useEffect(() => {
    if (visible) {
      if (field) {
        setName(field.name);
        setType(field.descriptor.type ?? 'string');
        setRequired(field.descriptor.required ?? false);
        setDescription(field.descriptor.description ?? '');
        setDefaultValue(formatDefaultValue(field.descriptor.default));
        setDefaultTouched(false);
        setItemsType(field.descriptor.items?.type ?? 'string');
        setConstraints(field.descriptor.constraints ?? {});
        setUiHints(field.descriptor.ui ?? {});
      } else {
        setName('');
        setType('string');
        setRequired(false);
        setDescription('');
        setDefaultValue('');
        setDefaultTouched(false);
        setItemsType('string');
        setConstraints({});
        setUiHints({});
      }
      setNameError(null);
    }
  }, [visible, field]);

  const handleOk = () => {
    const trimmed = name.trim();
    if (!trimmed) {
      setNameError(t('schema.field_name_required'));
      return;
    }
    if (trimmed.includes('.')) {
      setNameError(t('schema.field_name_no_dot'));
      return;
    }
    const otherNames = isEdit
      ? existingNames.filter((n) => n !== field!.name)
      : existingNames;
    if (otherNames.includes(trimmed)) {
      setNameError(t('schema.field_name_exists'));
      return;
    }

    // 编辑模式下以原 descriptor 为基底，避免未暴露键被覆盖丢失。
    const base: SchemaFieldDescriptor = isEdit && field ? { ...field.descriptor } : {};
    const descriptor: SchemaFieldDescriptor = {
      ...base,
      type: type as SchemaFieldDescriptor['type'],
    };

    if (type !== 'array') {
      delete descriptor.items;
    }
    if (type !== 'object') {
      delete descriptor.fields;
      delete descriptor.requiredKeys;
      delete descriptor.additionalProperties;
    }

    if (required) descriptor.required = true;
    else delete descriptor.required;

    if (description) descriptor.description = description;
    else delete descriptor.description;

    // 区分“未修改 default”与“主动清空 default”，避免编辑时误删既有默认值。
    if (defaultTouched) {
      if (defaultValue === '') {
        delete descriptor.default;
      } else {
        try {
          descriptor.default = JSON.parse(defaultValue);
        } catch {
          descriptor.default = defaultValue;
        }
      }
    }

    const cleanConstraints = Object.fromEntries(
      Object.entries(constraints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanConstraints).length > 0) {
      descriptor.constraints = cleanConstraints as SchemaFieldDescriptor['constraints'];
    } else {
      delete descriptor.constraints;
    }

    const cleanHints = Object.fromEntries(
      Object.entries(uiHints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanHints).length > 0) {
      descriptor.ui = cleanHints as NonNullable<SchemaFieldDescriptor['ui']>;
    } else {
      delete descriptor.ui;
    }

    if (type === 'array' && itemsType) {
      const nextItems: SchemaFieldDescriptor = {
        ...(descriptor.items ?? {}),
        type: itemsType as SchemaFieldDescriptor['type'],
      };
      if (itemsType !== 'object') {
        // itemsType 从 object 切走时清理对象专属键，避免残留的 fields 等结构污染非对象数组元素。
        delete nextItems.fields;
        delete nextItems.requiredKeys;
        delete nextItems.additionalProperties;
      }
      descriptor.items = nextItems;
    }

    const node: SchemaNode = { name: trimmed, descriptor };
    const keepChildren = descriptor.type === 'object'
      || (descriptor.type === 'array' && descriptor.items?.type === 'object');
    if (isEdit && field!.children && keepChildren) {
      node.children = field!.children;
    }
    onSave(node);
  };

  const collapseItems = [
    {
      key: 'constraints',
      label: t('schema.constraints'),
      children: (
        <ConstraintsSection
          type={type}
          constraints={constraints}
          onChange={setConstraints}
        />
      ),
    },
    {
      key: 'uiHints',
      label: t('schema.ui_hints'),
      children: (
        <UiHintsSection
          hints={uiHints}
          onChange={setUiHints}
        />
      ),
    },
  ];

  return (
    <Modal
      title={isEdit ? t('schema.edit_field') : t('schema.add_field')}
      open={visible}
      onOk={handleOk}
      onCancel={onCancel}
      data-testid="field-edit-modal"
      destroyOnClose
      width={520}
    >
      <Form layout="vertical">
        <Form.Item
          label={t('schema.field_name')}
          validateStatus={nameError ? 'error' : undefined}
          help={nameError}
        >
          <Input
            value={name}
            onChange={(e) => { setName(e.target.value); setNameError(null); }}
            placeholder={t('schema.field_name_placeholder')}
            data-testid="field-name-input"
          />
        </Form.Item>
        <Form.Item label={t('schema.type')}>
          <Select
            value={type}
            onChange={(v) => { setType(v); setConstraints({}); }}
            data-testid="field-type-select"
            options={FIELD_TYPES.map((ft) => ({ label: ft, value: ft }))}
          />
        </Form.Item>
        <Form.Item label={t('schema.required_label')}>
          <Switch
            checked={required}
            onChange={setRequired}
            data-testid="field-required-switch"
          />
        </Form.Item>
        <Form.Item label={t('schema.description')}>
          <Input.TextArea
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            rows={2}
            data-testid="field-description-input"
          />
        </Form.Item>
        <Form.Item label={t('schema.default_value')}>
          <Input
            value={defaultValue}
            onChange={(e) => {
              setDefaultValue(e.target.value);
              setDefaultTouched(true);
            }}
            placeholder={t('schema.default_placeholder')}
            data-testid="field-default-input"
          />
        </Form.Item>
        {type === 'array' && (
          <Form.Item label={t('schema.items_type')}>
            <Select
              value={itemsType}
              onChange={setItemsType}
              data-testid="field-items-type-select"
              options={ITEM_TYPES.map((ft) => ({ label: ft, value: ft }))}
            />
          </Form.Item>
        )}
      </Form>
      <Collapse items={collapseItems} size="small" />
    </Modal>
  );
};
