import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@monaco-editor/react', () => ({
  default: (props: any) => (
    <div data-testid="mock-monaco-inner" data-readonly={props.options?.readOnly}>
      <textarea value={props.value} onChange={(e: any) => props.onChange?.(e.target.value)} />
    </div>
  ),
}));

import { MonacoEditor } from '../MonacoEditor';

function renderEditor(props: Partial<Parameters<typeof MonacoEditor>[0]> = {}) {
  return render(
    <ConfigProvider>
      <MonacoEditor content="hello world" {...props} />
    </ConfigProvider>,
  );
}

describe('MonacoEditor', () => {
  it('renders editor container', () => {
    renderEditor();
    expect(screen.getByTestId('monaco-editor')).toBeDefined();
    expect(screen.getByTestId('mock-monaco-inner')).toBeDefined();
  });

  it('passes content to editor', () => {
    renderEditor({ content: 'test code' });
    const textarea = screen.getByDisplayValue('test code');
    expect(textarea).toBeDefined();
  });

  it('shows file size warning for large files', () => {
    renderEditor({ fileSize: 2 * 1024 * 1024 });
    expect(screen.getByTestId('file-size-warning')).toBeDefined();
  });
});
