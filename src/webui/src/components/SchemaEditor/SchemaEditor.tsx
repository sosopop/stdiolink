import React, { useEffect } from 'react';
import { Tabs } from 'antd';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import { VisualEditor } from './VisualEditor';
import { JsonEditor } from './JsonEditor';
import { PreviewEditor } from './PreviewEditor';
import { SchemaToolbar } from './SchemaToolbar';

interface SchemaEditorProps {
  serviceId: string;
}

export const SchemaEditor: React.FC<SchemaEditorProps> = ({ serviceId }) => {
  const { activeMode, setActiveMode, loadSchema } = useSchemaEditorStore();

  useEffect(() => {
    loadSchema(serviceId);
  }, [serviceId, loadSchema]);

  const items = [
    {
      key: 'visual',
      label: 'Visual',
      children: <VisualEditor />,
    },
    {
      key: 'json',
      label: 'JSON',
      children: <JsonEditor />,
    },
    {
      key: 'preview',
      label: 'Preview',
      children: <PreviewEditor />,
    },
  ];

  return (
    <div data-testid="schema-editor">
      <Tabs
        activeKey={activeMode}
        onChange={(key) => setActiveMode(key as 'visual' | 'json' | 'preview')}
        items={items}
        data-testid="schema-mode-tabs"
      />
      <SchemaToolbar serviceId={serviceId} />
    </div>
  );
};
