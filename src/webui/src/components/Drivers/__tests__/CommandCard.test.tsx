import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { CommandCard } from '../CommandCard';

const mockCommand = {
  name: 'add',
  description: 'Add two numbers',
  params: [
    { name: 'a', type: 'double', required: true, description: 'First number' },
    { name: 'b', type: 'double', required: true, description: 'Second number' },
  ],
  returns: { type: 'double', description: 'Sum result' },
};

describe('CommandCard', () => {
  it('renders command name and description', () => {
    render(<ConfigProvider><CommandCard command={mockCommand} driverId="drv1" onTest={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('command-add')).toBeDefined();
    expect(screen.getByText('add')).toBeDefined();
  });

  it('renders params table', () => {
    render(<ConfigProvider><CommandCard command={mockCommand} driverId="drv1" onTest={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('params-table')).toBeDefined();
  });

  it('renders return type', () => {
    render(<ConfigProvider><CommandCard command={mockCommand} driverId="drv1" onTest={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('return-type-add')).toBeDefined();
    expect(screen.getByTestId('return-type-add').textContent).toBe('double');
  });

  it('calls onTest when test button clicked', () => {
    const onTest = vi.fn();
    render(<ConfigProvider><CommandCard command={mockCommand} driverId="drv1" onTest={onTest} /></ConfigProvider>);
    fireEvent.click(screen.getByTestId('test-cmd-add'));
    expect(onTest).toHaveBeenCalledWith('add');
  });

  it('shows empty params for no-param command', () => {
    const cmd = { name: 'ping', params: [], returns: { type: 'string' } };
    render(<ConfigProvider><CommandCard command={cmd} driverId="drv1" onTest={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('empty-params')).toBeDefined();
  });
});
