import React, { useCallback } from 'react';
import Editor from '@monaco-editor/react';
import { Alert } from 'antd';

const SIZE_LIMIT = 1024 * 1024; // 1MB

function detectLanguage(path: string): string {
  if (path.endsWith('.json')) return 'json';
  if (path.endsWith('.md')) return 'markdown';
  return 'javascript';
}

export interface MonacoEditorProps {
  content: string;
  filePath?: string;
  language?: string;
  onChange?: (value: string) => void;
  onSave?: () => void;
  readOnly?: boolean;
  fileSize?: number;
}

export const MonacoEditor: React.FC<MonacoEditorProps> = ({
  content,
  filePath,
  language,
  onChange,
  onSave,
  readOnly = false,
  fileSize,
}) => {
  const lang = language ?? detectLanguage(filePath ?? '');

  const handleMount = useCallback(
    (editor: any) => {
      if (onSave) {
        editor.addCommand(2048 + 49 /* KeyMod.CtrlCmd | KeyCode.KeyS */, () => {
          onSave();
        });
      }
    },
    [onSave],
  );

  if (fileSize && fileSize > SIZE_LIMIT) {
    return (
      <Alert
        type="warning"
        message="File too large"
        description="Files larger than 1MB cannot be edited in the browser."
        data-testid="file-size-warning"
      />
    );
  }

  return (
    <div data-testid="monaco-editor" style={{ height: '100%', minHeight: 400 }}>
      <Editor
        height="100%"
        language={lang}
        value={content}
        theme="vs-dark"
        onChange={(v) => onChange?.(v ?? '')}
        onMount={handleMount}
        options={{
          readOnly,
          minimap: { enabled: false },
          fontSize: 13,
          lineNumbers: 'on',
          scrollBeyondLastLine: false,
          wordWrap: 'on',
        }}
      />
    </div>
  );
};
