import { describe, it, expect } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ProcessTreeNodeComponent } from '../ProcessTreeNode';

const makeNode = (overrides = {}) => ({
  pid: 100,
  name: 'node',
  commandLine: '/usr/bin/node',
  status: 'running',
  resources: { cpuPercent: 25, memoryRssBytes: 1024 * 1024 * 256, threadCount: 4 },
  children: [],
  ...overrides,
});

describe('ProcessTreeNodeComponent', () => {
  it('renders node info', () => {
    render(<ProcessTreeNodeComponent node={makeNode()} level={0} />);
    expect(screen.getByTestId('tree-node-100')).toBeDefined();
    expect(screen.getByTestId('pid-100')).toBeDefined();
    expect(screen.getByText('node')).toBeDefined();
  });

  it('renders children', () => {
    const node = makeNode({
      children: [makeNode({ pid: 101, name: 'child' })],
    });
    render(<ProcessTreeNodeComponent node={node} level={0} />);
    expect(screen.getByTestId('tree-node-101')).toBeDefined();
    expect(screen.getByTestId('children-100')).toBeDefined();
  });

  it('collapses and expands children', () => {
    const node = makeNode({
      children: [makeNode({ pid: 101, name: 'child' })],
    });
    render(<ProcessTreeNodeComponent node={node} level={0} />);
    expect(screen.getByTestId('children-100')).toBeDefined();
    fireEvent.click(screen.getByTestId('toggle-100'));
    expect(screen.queryByTestId('children-100')).toBeNull();
    fireEvent.click(screen.getByTestId('toggle-100'));
    expect(screen.getByTestId('children-100')).toBeDefined();
  });

  it('shows green color for low CPU', () => {
    render(<ProcessTreeNodeComponent node={makeNode({ resources: { cpuPercent: 10, memoryRssBytes: 1024, threadCount: 1 } })} level={0} />);
    const cpuEl = screen.getByTestId('cpu-100');
    expect(cpuEl.style.color).toBe('rgb(82, 196, 26)');
  });

  it('shows red color for high CPU', () => {
    render(<ProcessTreeNodeComponent node={makeNode({ resources: { cpuPercent: 95, memoryRssBytes: 1024, threadCount: 1 } })} level={0} />);
    const cpuEl = screen.getByTestId('cpu-100');
    expect(cpuEl.style.color).toBe('rgb(255, 77, 79)');
  });

  it('formats memory as MB', () => {
    render(<ProcessTreeNodeComponent node={makeNode({ resources: { cpuPercent: 10, memoryRssBytes: 1024 * 1024 * 256, threadCount: 1 } })} level={0} />);
    expect(screen.getByText('Mem: 256.0 MB')).toBeDefined();
  });
});
