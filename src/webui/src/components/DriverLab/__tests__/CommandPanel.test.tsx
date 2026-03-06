import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import React from 'react';
import { ConfigProvider } from 'antd';
import { CommandPanel } from '../CommandPanel';

const mockCommands = [
  {
    name: 'add',
    description: 'Add two numbers',
    params: [
      { name: 'a', type: 'double' as const, required: true, description: 'First' },
      { name: 'b', type: 'double' as const, required: true, description: 'Second' },
    ],
    returns: { type: 'double' },
    examples: [
      {
        description: 'quick add',
        mode: 'console',
        params: { a: 1, b: 2 },
      },
      {
        description: 'quick add stdio',
        mode: 'stdio',
        params: { a: 11, b: 22 },
      },
    ],
  },
  {
    name: 'ping',
    params: [],
    returns: { type: 'string' },
  },
];

function renderPanel(overrides = {}) {
  const props = {
    commands: mockCommands,
    selectedCommand: null as string | null,
    commandParams: {},
    executing: false,
    connected: true,
    driverId: 'calc',
    onSelectCommand: vi.fn(),
    onParamsChange: vi.fn(),
    onExec: vi.fn(),
    onCancel: vi.fn(),
    ...overrides,
  };
  return { ...render(<ConfigProvider><CommandPanel {...props} /></ConfigProvider>), props };
}

function renderPanelHarness(initialParams: Record<string, unknown>) {
  const onSelectCommand = vi.fn();
  const onExec = vi.fn();
  const onCancel = vi.fn();

  const Harness = () => {
    const [params, setParams] = React.useState<Record<string, unknown>>(initialParams);
    return (
      <ConfigProvider>
        <CommandPanel
          commands={mockCommands}
          selectedCommand="add"
          commandParams={params}
          executing={false}
          connected={true}
          driverId="calc"
          onSelectCommand={onSelectCommand}
          onParamsChange={setParams}
          onExec={onExec}
          onCancel={onCancel}
        />
      </ConfigProvider>
    );
  };

  return render(<Harness />);
}

describe('CommandPanel', () => {
  it('renders command panel with commands', () => {
    renderPanel();
    expect(screen.getByTestId('command-panel')).toBeDefined();
    expect(screen.getByTestId('command-select')).toBeDefined();
  });

  it('shows waiting message when no commands', () => {
    renderPanel({ commands: [] });
    expect(screen.getByTestId('waiting-meta')).toBeDefined();
  });

  it('exec button disabled when not connected', () => {
    renderPanel({ connected: false });
    const btn = screen.getByTestId('exec-btn');
    expect(btn.closest('button')?.disabled).toBe(true);
  });

  it('exec button disabled when no command selected', () => {
    renderPanel({ selectedCommand: null });
    const btn = screen.getByTestId('exec-btn');
    expect(btn.closest('button')?.disabled).toBe(true);
  });

  it('exec button enabled when connected and command selected', () => {
    renderPanel({ selectedCommand: 'add' });
    const btn = screen.getByTestId('exec-btn');
    expect(btn.closest('button')?.disabled).toBe(false);
  });

  it('cancel button disabled when not executing', () => {
    renderPanel({ executing: false });
    const btn = screen.getByTestId('cancel-btn');
    expect(btn.closest('button')?.disabled).toBe(true);
  });

  it('cancel button enabled when executing', () => {
    const { props } = renderPanel({ executing: true, selectedCommand: 'add' });
    const btn = screen.getByTestId('cancel-btn');
    expect(btn.closest('button')?.disabled).toBe(false);
    fireEvent.click(btn);
    expect(props.onCancel).toHaveBeenCalled();
  });

  it('applies selected example params', () => {
    const { props } = renderPanel({ selectedCommand: 'add' });
    fireEvent.click(screen.getByTestId('apply-example-0'));
    expect(props.onParamsChange).toHaveBeenCalledWith({ a: 1, b: 2 });
    expect(screen.getByTestId('apply-example-1')).toBeDefined();
  });

  it('does not render examples block when command has no examples', () => {
    renderPanel({ selectedCommand: 'ping' });
    expect(screen.queryByTestId('command-examples')).toBeNull();
  });

  it('preview follows current params and changes only after apply updates state', () => {
    renderPanelHarness({ a: 9, b: 9 });
    expect(screen.getByTestId('cmdline-text').textContent).toBe('--cmd=add --a=9 --b=9');

    fireEvent.click(screen.getByTestId('apply-example-0'));
    expect(screen.getByTestId('cmdline-text').textContent).toBe('--cmd=add --a=1 --b=2');
  });
});
