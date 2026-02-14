import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';

vi.mock('@/stores/useSchemaEditorStore', () => {
  const store = {
    nodes: [],
    addField: vi.fn(),
    updateField: vi.fn(),
    removeField: vi.fn(),
    moveField: vi.fn(),
  };
  return {
    useSchemaEditorStore: Object.assign(
      vi.fn().mockImplementation((sel?: any) => (sel ? sel(store) : store)),
      { getState: () => store, setState: (s: any) => Object.assign(store, s) },
    ),
  };
});

import { useSchemaEditorStore } from '@/stores/useSchemaEditorStore';
import { VisualEditor } from '../VisualEditor';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

describe('VisualEditor', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('shows empty state when no nodes', () => {
    (useSchemaEditorStore as any).setState({ nodes: [] });
    wrap(<VisualEditor />);
    expect(screen.getByTestId('visual-empty')).toBeInTheDocument();
    expect(screen.getByTestId('visual-add-first')).toBeInTheDocument();
  });

  it('renders field cards for nodes', () => {
    (useSchemaEditorStore as any).setState({
      nodes: [
        { name: 'host', descriptor: { type: 'string' } },
        { name: 'port', descriptor: { type: 'int' } },
      ],
    });
    wrap(<VisualEditor />);
    expect(screen.getByTestId('field-card-host')).toBeInTheDocument();
    expect(screen.getByTestId('field-card-port')).toBeInTheDocument();
  });

  it('shows add button when nodes exist', () => {
    (useSchemaEditorStore as any).setState({
      nodes: [{ name: 'host', descriptor: { type: 'string' } }],
    });
    wrap(<VisualEditor />);
    expect(screen.getByTestId('visual-add-btn')).toBeInTheDocument();
  });

  it('opens modal on add button click', () => {
    (useSchemaEditorStore as any).setState({ nodes: [] });
    wrap(<VisualEditor />);
    fireEvent.click(screen.getByTestId('visual-add-first'));
    expect(screen.getByText('Add Field')).toBeInTheDocument();
  });
});
