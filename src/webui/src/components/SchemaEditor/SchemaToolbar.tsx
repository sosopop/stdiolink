import React from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Space, Tag } from 'antd';
import { SaveOutlined, UndoOutlined, CheckCircleOutlined } from '@ant-design/icons';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';

interface SchemaToolbarProps {
  serviceId: string;
}

export const SchemaToolbar: React.FC<SchemaToolbarProps> = ({ serviceId }) => {
  const { t } = useTranslation();
  const { dirty, saving, validating, validationErrors, validate, save, reset } =
    useSchemaEditorStore();

  return (
    <div
      data-testid="schema-toolbar"
      style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 16 }}
    >
      <Space>
        <Button
          icon={<CheckCircleOutlined />}
          onClick={() => validate(serviceId)}
          loading={validating}
          data-testid="schema-validate-btn"
        >
          {t('schema.validate')}
        </Button>
        <Button
          type="primary"
          icon={<SaveOutlined />}
          onClick={() => save(serviceId)}
          loading={saving}
          disabled={!dirty}
          data-testid="schema-save-btn"
        >
          {t('schema.save')}
        </Button>
        <Button
          icon={<UndoOutlined />}
          onClick={reset}
          disabled={!dirty}
          data-testid="schema-reset-btn"
        >
          {t('schema.reset')}
        </Button>
      </Space>
      <Space>
        {dirty && <Tag color="orange">{t('schema.unsaved_changes')}</Tag>}
        {validationErrors.length > 0 && (
          <Tag color="red" data-testid="validation-error-tag">
            {validationErrors[0]}
          </Tag>
        )}
      </Space>
    </div>
  );
};
