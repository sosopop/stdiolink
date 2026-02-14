import React from 'react';
import { Button, Space, Tag } from 'antd';
import { SaveOutlined, UndoOutlined, CheckCircleOutlined } from '@ant-design/icons';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';

interface SchemaToolbarProps {
  serviceId: string;
}

export const SchemaToolbar: React.FC<SchemaToolbarProps> = ({ serviceId }) => {
  const { dirty, saving, validating, validationErrors, validate, save, reset } =
    useSchemaEditorStore();

  return (
    <div
      data-testid="schema-toolbar"
      style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
    >
      <Space>
        <Button
          icon={<CheckCircleOutlined />}
          onClick={() => validate(serviceId)}
          loading={validating}
          data-testid="schema-validate-btn"
        >
          Validate
        </Button>
        <Button
          type="primary"
          icon={<SaveOutlined />}
          onClick={() => save(serviceId)}
          loading={saving}
          disabled={!dirty}
          data-testid="schema-save-btn"
        >
          Save
        </Button>
        <Button
          icon={<UndoOutlined />}
          onClick={reset}
          disabled={!dirty}
          data-testid="schema-reset-btn"
        >
          Reset
        </Button>
      </Space>
      <Space>
        {dirty && <Tag color="orange">Unsaved changes</Tag>}
        {validationErrors.length > 0 && (
          <Tag color="red" data-testid="validation-error-tag">
            {validationErrors[0]}
          </Tag>
        )}
      </Space>
    </div>
  );
};
