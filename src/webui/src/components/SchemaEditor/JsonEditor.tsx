import React from 'react';
import { Alert } from 'antd';
import { MonacoEditor } from '@/components/CodeEditor/MonacoEditor';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';

export const JsonEditor: React.FC = () => {
  const { jsonText, jsonError, setJsonText } = useSchemaEditorStore();

  return (
    <div data-testid="json-editor" style={{ height: 'calc(100vh - 320px)', display: 'flex', flexDirection: 'column' }}>
      {jsonError && (
        <Alert
          type="error"
          message={jsonError}
          style={{ marginBottom: 8 }}
          data-testid="json-error"
        />
      )}
      <div style={{ flex: 1, minHeight: 0 }}>
        <MonacoEditor
          content={jsonText}
          language="json"
          onChange={setJsonText}
        />
      </div>
    </div>
  );
};
