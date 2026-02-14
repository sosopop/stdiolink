import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ProcessTree } from '../ProcessTree';

const mockTree = {
  pid: 100,
  name: 'root',
  commandLine: '/usr/bin/root',
  status: 'running',
  resources: { cpuPercent: 25, memoryRssBytes: 1024 * 1024 * 256, threadCount: 4 },
  children: [
    {
      pid: 101,
      name: 'child1',
      commandLine: '/usr/bin/child1',
      status: 'running',
      resources: { cpuPercent: 85, memoryRssBytes: 1024 * 1024 * 128, threadCount: 2 },
      children: [],
    },
  ],
};

const mockSummary = {
  totalProcesses: 2,
  totalCpuPercent: 110,
  totalMemoryRssBytes: 1024 * 1024 * 384,
  totalThreads: 6,
};

describe('ProcessTree', () => {
  it('renders tree with nodes', () => {
    render(<ConfigProvider><ProcessTree tree={mockTree} summary={mockSummary} /></ConfigProvider>);
    expect(screen.getByTestId('process-tree')).toBeDefined();
    expect(screen.getByTestId('tree-node-100')).toBeDefined();
    expect(screen.getByTestId('tree-node-101')).toBeDefined();
  });

  it('renders summary card', () => {
    render(<ConfigProvider><ProcessTree tree={mockTree} summary={mockSummary} /></ConfigProvider>);
    expect(screen.getByTestId('process-tree-summary')).toBeDefined();
  });

  it('shows empty state when no tree', () => {
    render(<ConfigProvider><ProcessTree tree={null} summary={null} /></ConfigProvider>);
    expect(screen.getByTestId('empty-process-tree')).toBeDefined();
  });
});
