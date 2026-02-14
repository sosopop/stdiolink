import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Modal, Form, Input, Select, Switch, Collapse } from 'antd';
import type { SchemaNode, SchemaFieldDescriptor } from '@/utils/schemaPath';
import { ConstraintsSection } from './ConstraintsSection';
import { UiHintsSection } from './UiHintsSection';

const FIELD_TYPES = ['string', 'int', 'int64', 'double', 'bool', 'object', 'array', 'enum', 'any'] as const;

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
  const [constraints, setConstraints] = useState<Record<string, unknown>>({});
  const [uiHints, setUiHints] = useState<Record<string, unknown>>({});
  const [nameError, setNameError] = useState<string | null>(null);

  const isEdit = field !== null;

  useEffect(() => {
    if (visible) {
      if (field) {
        setName(field.name);
        setType(field.descriptor.type ?? 'string');
        setRequired(field.descriptor.required ?? false);
        setDescription(field.descriptor.description ?? '');
        setDefaultValue(field.descriptor.default !== undefined ? String(field.descriptor.default) : '');
        setConstraints(field.descriptor.constraints ?? {});
        setUiHints((field.descriptor as any).ui ?? {});
      } else {
        setName('');
        setType('string');
        setRequired(false);
        setDescription('');
        setDefaultValue('');
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
    const otherNames = isEdit
      ? existingNames.filter((n) => n !== field!.name)
      : existingNames;
    if (otherNames.includes(trimmed)) {
      setNameError(t('schema.field_name_exists'));
      return;
    }

    const descriptor: SchemaFieldDescriptor = { type: type as any };
    if (required) descriptor.required = true;
    if (description) descriptor.description = description;
    if (defaultValue) {
      try {
        descriptor.default = JSON.parse(defaultValue);
      } catch {
        descriptor.default = defaultValue;
      }
    }
    const cleanConstraints = Object.fromEntries(
      Object.entries(constraints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanConstraints).length > 0) {
      descriptor.constraints = cleanConstraints as any;
    }
    const cleanHints = Object.fromEntries(
      Object.entries(uiHints).filter(([, v]) => v !== undefined && v !== null && v !== ''),
    );
    if (Object.keys(cleanHints).length > 0) {
      (descriptor as any).ui = cleanHints;
    }

    const node: SchemaNode = { name: trimmed, descriptor };
    if (isEdit && field!.children) {
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
          hints={uiHints as any}
          onChange={setUiHints as any}
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
            onChange={(e) => setDefaultValue(e.target.value)}
            placeholder={t('schema.default_placeholder')}
            data-testid="field-default-input"
          />
        </Form.Item>
      </Form>
      <Collapse items={collapseItems} size="small" />
    </Modal>
  );
};
