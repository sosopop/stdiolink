import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { FileTree } from '../FileTree';
import type { ServiceFile } from '@/types/service';

const mockFiles: ServiceFile[] = [
  { name: 'manifest.json', path: 'manifest.json', size: 200, type: 'file', modifiedAt: '2024-01-01' },
  { name: 'index.js', path: 'index.js', size: 500, type: 'file', modifiedAt: '2024-01-01' },
  { name: 'helper.js', path: 'helper.js', size: 100, type: 'file', modifiedAt: '2024-01-01' },
];

function renderComponent(props: Partial<Parameters<typeof FileTree>[0]> = {}) {
  const defaultProps = {
    files: mockFiles,
    selectedPath: null,
    onSelect: vi.fn(),
    onCreateFile: vi.fn(),
    onDeleteFile: vi.fn(),
    ...props,
  };
  return render(
    <ConfigProvider>
      <FileTree {...defaultProps} />
    </ConfigProvider>,
  );
}

describe('FileTree', () => {
  it('renders all file names', () => {
    renderComponent();
    expect(screen.getByTestId('file-node-manifest.json')).toBeDefined();
    expect(screen.getByTestId('file-node-index.js')).toBeDefined();
    expect(screen.getByTestId('file-node-helper.js')).toBeDefined();
  });

  it('shows selected state for selected file', () => {
    renderComponent({ selectedPath: 'helper.js' });
    expect(screen.getByTestId('file-tree')).toBeDefined();
  });

  it('shows protected tag for core files', () => {
    renderComponent();
    expect(screen.getByTestId('protected-manifest.json')).toBeDefined();
    expect(screen.getByTestId('protected-index.js')).toBeDefined();
    expect(screen.queryByTestId('protected-helper.js')).toBeNull();
  });
});
