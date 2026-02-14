import React from 'react';
import { Alert } from 'antd';
import { MonacoEditor } from '@/components/CodeEditor/MonacoEditor';
import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';

export const JsonEditor: React.FC = () => {
  const { jsonText, jsonError, setJsonText } = useSchemaEditorStore();

  return (
    <div data-testid="json-editor">
      {jsonError && (
        <Alert
          type="error"
          message={jsonError}
          style={{ marginBottom: 8 }}
          data-testid="json-error"
        />
      )}
      <MonacoEditor
        content={jsonText}
        language="json"
        onChange={setJsonText}
      />
    </div>
  );
};
