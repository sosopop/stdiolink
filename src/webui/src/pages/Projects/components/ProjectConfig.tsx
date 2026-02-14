import React, { useState } from 'react';
import { Button, message } from 'antd';
import { SchemaForm } from '@/components/SchemaForm/SchemaForm';
import type { FieldMeta } from '@/types/service';

interface ProjectConfigProps {
  config: Record<string, unknown>;
  schema: FieldMeta[];
  onSave: (config: Record<string, unknown>) => Promise<boolean>;
}

export const ProjectConfig: React.FC<ProjectConfigProps> = ({ config, schema, onSave }) => {
  const [value, setValue] = useState<Record<string, unknown>>(config);
  const [saving, setSaving] = useState(false);

  const handleSave = async () => {
    setSaving(true);
    const ok = await onSave(value);
    if (ok) message.success('Configuration saved');
    else message.error('Failed to save configuration');
    setSaving(false);
  };

  return (
    <div data-testid="project-config">
      <SchemaForm schema={schema} value={value} onChange={setValue} />
      <Button
        type="primary"
        onClick={handleSave}
        loading={saving}
        style={{ marginTop: 16 }}
        data-testid="save-config-btn"
      >
        Save Configuration
      </Button>
    </div>
  );
};
