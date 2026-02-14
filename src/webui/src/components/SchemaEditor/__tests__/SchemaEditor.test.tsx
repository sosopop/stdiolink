import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@monaco-editor/react', () => ({
  default: (props: any) => (
    <div data-testid="mock-monaco-inner">
      <textarea value={props.value} onChange={(e: any) => props.onChange?.(e.target.value)} />
    </div>
  ),
}));

vi.mock('@/stores/useSchemaEditorStore', () => {
  const store = {
    nodes: [],
    originalNodes: [],
    activeMode: 'visual',
    jsonText: '{}',
    jsonError: null,
    dirty: false,
    validationErrors: [],
    validating: false,
    saving: false,
    setActiveMode: vi.fn(),
    loadSchema: vi.fn(),
    addField: vi.fn(),
    updateField: vi.fn(),
    removeField: vi.fn(),
    moveField: vi.fn(),
    setJsonText: vi.fn(),
    syncFromJson: vi.fn(),
    syncToJson: vi.fn(),
    validate: vi.fn(),
    save: vi.fn(),
    reset: vi.fn(),
  };
  return {
    useSchemaEditorStore: Object.assign(
      vi.fn().mockImplementation((sel?: any) => (sel ? sel(store) : store)),
      { getState: () => store, setState: (s: any) => Object.assign(store, s) },
    ),
  };
});

vi.mock('@/components/SchemaForm/SchemaForm', () => ({
  SchemaForm: () => <div data-testid="mock-schema-form" />,
}));

import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import { SchemaEditor } from '../SchemaEditor';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

describe('SchemaEditor', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('renders with mode tabs', () => {
    wrap(<SchemaEditor serviceId="svc1" />);
    expect(screen.getByTestId('schema-editor')).toBeInTheDocument();
    expect(screen.getByText('Visual')).toBeInTheDocument();
    expect(screen.getByText('JSON')).toBeInTheDocument();
    expect(screen.getByText('Preview')).toBeInTheDocument();
  });

  it('calls loadSchema on mount', () => {
    wrap(<SchemaEditor serviceId="svc1" />);
    const store = (useSchemaEditorStore as any).getState();
    expect(store.loadSchema).toHaveBeenCalledWith('svc1');
  });

  it('renders toolbar', () => {
    wrap(<SchemaEditor serviceId="svc1" />);
    expect(screen.getByTestId('schema-toolbar')).toBeInTheDocument();
    expect(screen.getByTestId('schema-validate-btn')).toBeInTheDocument();
    expect(screen.getByTestId('schema-save-btn')).toBeInTheDocument();
    expect(screen.getByTestId('schema-reset-btn')).toBeInTheDocument();
  });

  it('shows visual editor by default', () => {
    wrap(<SchemaEditor serviceId="svc1" />);
    expect(screen.getByTestId('visual-editor')).toBeInTheDocument();
  });

  it('shows unsaved changes tag when dirty', () => {
    (useSchemaEditorStore as any).setState({ dirty: true });
    wrap(<SchemaEditor serviceId="svc1" />);
    expect(screen.getByText('Unsaved changes')).toBeInTheDocument();
  });

  it('shows validation error tag', () => {
    (useSchemaEditorStore as any).setState({ validationErrors: ['Schema invalid'] });
    wrap(<SchemaEditor serviceId="svc1" />);
    expect(screen.getByText('Schema invalid')).toBeInTheDocument();
  });
});
