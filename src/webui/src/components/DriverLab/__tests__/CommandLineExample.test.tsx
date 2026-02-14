import { describe, it, expect } from 'vitest';
import { buildCommandLine } from '../CommandLineExample';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { CommandLineExample } from '../CommandLineExample';

describe('buildCommandLine', () => {
  it('returns empty when no driver or command', () => {
    expect(buildCommandLine(null, null, {})).toBe('');
    expect(buildCommandLine('drv', null, {})).toBe('');
    expect(buildCommandLine(null, 'cmd', {})).toBe('');
  });

  it('generates basic command line', () => {
    expect(buildCommandLine('calc', 'add', {})).toBe('calc --cmd=add');
  });

  it('appends params as --key=value', () => {
    const result = buildCommandLine('calc', 'add', { a: 1, b: 2 });
    expect(result).toBe('calc --cmd=add --a=1 --b=2');
  });

  it('wraps string values with spaces in quotes', () => {
    const result = buildCommandLine('drv', 'cmd', { name: 'hello world' });
    expect(result).toBe('drv --cmd=cmd --name="hello world"');
  });

  it('serializes object values as compact JSON', () => {
    const result = buildCommandLine('drv', 'cmd', { data: { x: 1 } });
    expect(result).toContain('--data="{"x":1}"');
  });

  it('skips null and empty string params', () => {
    const result = buildCommandLine('drv', 'cmd', { a: 1, b: null, c: '' });
    expect(result).toBe('drv --cmd=cmd --a=1');
  });

  it('handles boolean values', () => {
    const result = buildCommandLine('drv', 'cmd', { verbose: true, quiet: false });
    expect(result).toBe('drv --cmd=cmd --verbose=true --quiet=false');
  });

  it('handles array values', () => {
    const result = buildCommandLine('drv', 'cmd', { ids: [1, 2, 3] });
    expect(result).toContain('--ids="[1,2,3]"');
  });
});

describe('CommandLineExample component', () => {
  it('shows placeholder when no command selected', () => {
    render(<ConfigProvider><CommandLineExample driverId="drv" command={null} params={{}} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-placeholder')).toBeDefined();
  });

  it('shows command line text when command selected', () => {
    render(<ConfigProvider><CommandLineExample driverId="calc" command="add" params={{ a: 1 }} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-text')).toBeDefined();
    expect(screen.getByTestId('cmdline-text').textContent).toBe('calc --cmd=add --a=1');
  });

  it('renders copy button', () => {
    render(<ConfigProvider><CommandLineExample driverId="calc" command="add" params={{}} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-copy')).toBeDefined();
  });
});
