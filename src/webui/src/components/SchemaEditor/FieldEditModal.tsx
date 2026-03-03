import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Modal, Form, Input, Select, Switch, Collapse, Row, Col, Typography } from 'antd';
import type { SchemaNode, SchemaFieldDescriptor } from '@/utils/schemaPath';

const { Text } = Typography;
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

    const base: SchemaFieldDescriptor = isEdit && field ? { ...field.descriptor } : {};
    const descriptor: SchemaFieldDescriptor = {
      ...base,
      type: type as SchemaFieldDescriptor['type'],
    };

    if (type !== 'array') delete descriptor.items;
    if (type !== 'object') {
      delete descriptor.fields;
      delete descriptor.requiredKeys;
      delete descriptor.additionalProperties;
    }

    if (required) descriptor.required = true;
    else delete descriptor.required;

    if (description) descriptor.description = description;
    else delete descriptor.description;

    if (defaultTouched) {
      if (defaultValue === '') {
        delete descriptor.default;
      } else {
        try { descriptor.default = JSON.parse(defaultValue); }
        catch { descriptor.default = defaultValue; }
      }
    }

    const cleanConstraints = Object.fromEntries(
      Object.entries(constraints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanConstraints).length > 0) {
      descriptor.constraints = cleanConstraints as SchemaFieldDescriptor['constraints'];
    } else delete descriptor.constraints;

    const cleanHints = Object.fromEntries(
      Object.entries(uiHints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanHints).length > 0) {
      descriptor.ui = cleanHints as NonNullable<SchemaFieldDescriptor['ui']>;
    } else delete descriptor.ui;

    if (type === 'array' && itemsType) {
      const nextItems: SchemaFieldDescriptor = {
        ...(descriptor.items ?? {}),
        type: itemsType as SchemaFieldDescriptor['type'],
      };
      if (itemsType !== 'object') {
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
      label: <span style={{ fontSize: 13, fontWeight: 600 }}>{t('schema.constraints')}</span>,
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
      label: <span style={{ fontSize: 13, fontWeight: 600 }}>{t('schema.ui_hints')}</span>,
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
      title={
        <div style={{ paddingBottom: 12, borderBottom: '1px solid var(--border-subtle)', marginBottom: 16 }}>
          <Text strong style={{ fontSize: 16 }}>{isEdit ? t('schema.edit_field') : t('schema.add_field')}</Text>
        </div>
      }
      open={visible}
      onOk={handleOk}
      onCancel={onCancel}
      data-testid="field-edit-modal"
      destroyOnClose
      width={600}
      centered
      bodyStyle={{ padding: '0 4px' }}
    >
      <Form layout="vertical" requiredMark={false}>
        <Row gutter={24}>
          <Col span={14}>
            <Form.Item
              label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.field_name').toUpperCase()}</Text>}
              validateStatus={nameError ? 'error' : undefined}
              help={nameError}
            >
              <Input
                value={name}
                onChange={(e) => { setName(e.target.value); setNameError(null); }}
                placeholder={t('schema.field_name_placeholder')}
                data-testid="field-name-input"
                style={{ borderRadius: 8 }}
              />
            </Form.Item>
          </Col>
          <Col span={10}>
            <Form.Item label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.type').toUpperCase()}</Text>}>
              <Select
                value={type}
                onChange={(v) => { setType(v); setConstraints({}); }}
                data-testid="field-type-select"
                options={FIELD_TYPES.map((ft) => ({ label: ft, value: ft }))}
                style={{ width: '100%' }}
                dropdownStyle={{ borderRadius: 8 }}
              />
            </Form.Item>
          </Col>
        </Row>

        <Row gutter={24} align="middle">
          <Col span={14}>
            <Form.Item label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.default_value').toUpperCase()}</Text>}>
              <Input
                value={defaultValue}
                onChange={(e) => { setDefaultValue(e.target.value); setDefaultTouched(true); }}
                placeholder={t('schema.default_placeholder')}
                data-testid="field-default-input"
                style={{ borderRadius: 8 }}
              />
            </Form.Item>
          </Col>
          <Col span={10}>
            <Form.Item label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.required_label').toUpperCase()}</Text>}>
              <div style={{
                background: 'var(--surface-layer2)',
                padding: '4px 12px',
                borderRadius: 8,
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                height: 32
              }}>
                <Text type="secondary" style={{ fontSize: 12 }}>{required ? t('common.yes') : t('common.no')}</Text>
                <Switch
                  size="small"
                  checked={required}
                  onChange={setRequired}
                  data-testid="field-required-switch"
                />
              </div>
            </Form.Item>
          </Col>
        </Row>

        {type === 'array' && (
          <Form.Item
            label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.items_type').toUpperCase()}</Text>}
            style={{ background: 'var(--primary-dim)', padding: '12px', borderRadius: 12, border: '1px solid var(--primary-color)', opacity: 0.9 }}
          >
            <Select
              value={itemsType}
              onChange={setItemsType}
              data-testid="field-items-type-select"
              options={ITEM_TYPES.map((ft) => ({ label: ft, value: ft }))}
              dropdownStyle={{ borderRadius: 8 }}
            />
          </Form.Item>
        )}

        <Form.Item label={<Text strong style={{ fontSize: 12, opacity: 0.8 }}>{t('schema.description').toUpperCase()}</Text>}>
          <Input.TextArea
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            rows={2}
            placeholder="..."
            data-testid="field-description-input"
            style={{ borderRadius: 8 }}
          />
        </Form.Item>
      </Form>

      <div style={{ marginTop: 20 }}>
        <Collapse
          items={collapseItems}
          size="small"
          ghost
          expandIconPosition="end"
          style={{ background: 'var(--surface-layer1)', borderRadius: 12, border: '1px solid var(--border-subtle)' }}
        />
      </div>
    </Modal>
  );
};
