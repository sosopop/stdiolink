import { describe, it, expect } from 'vitest';
import { buildCommandLine, buildArgsLine } from '../CommandLineExample';
import { renderCliArgs } from '@/utils/cliArgs';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { CommandLineExample } from '../CommandLineExample';
import fs from 'node:fs';
import path from 'node:path';

function loadRenderCases() {
  const candidates = [
    path.resolve(process.cwd(), '../tests/data/cli_render_cases.json'),
    path.resolve(process.cwd(), 'src/tests/data/cli_render_cases.json'),
  ];
  const fixturePath = candidates.find((candidate) => fs.existsSync(candidate));
  if (!fixturePath) {
    throw new Error(`cli_render_cases.json not found in ${candidates.join(', ')}`);
  }
  return JSON.parse(fs.readFileSync(fixturePath, 'utf8')) as Array<{
    name: string;
    params: Record<string, unknown>;
    args: string[];
  }>;
}

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
    expect(result).toContain('--data.x=1');
  });

  it('renders null and empty string as canonical literals', () => {
    const result = buildCommandLine('drv', 'cmd', { a: 1, b: null, c: '' });
    expect(result).toBe('drv --cmd=cmd --a=1 --b=null --c=""');
  });

  it('handles boolean values', () => {
    const result = buildCommandLine('drv', 'cmd', { verbose: true, quiet: false });
    expect(result).toBe('drv --cmd=cmd --quiet=false --verbose=true');
  });

  it('handles array values', () => {
    const result = buildCommandLine('drv', 'cmd', { ids: [1, 2, 3] });
    expect(result).toBe('drv --cmd=cmd --ids[0]=1 --ids[1]=2 --ids[2]=3');
  });

  it('matches shared fixture cases', () => {
    const fixture = loadRenderCases();
    fixture.forEach((item) => {
      expect(buildArgsLine('run', item.params)).toBe(`--cmd=run ${item.args.join(' ')}`);
    });
  });
});

describe('buildArgsLine', () => {
  it('returns empty when no command', () => {
    expect(buildArgsLine(null, {})).toBe('');
  });

  it('generates args without driverId', () => {
    expect(buildArgsLine('add', {})).toBe('--cmd=add');
  });

  it('appends params as --key=value', () => {
    expect(buildArgsLine('add', { a: 1, b: 2 })).toBe('--cmd=add --a=1 --b=2');
  });
});

describe('renderCliArgs', () => {
  it('supports alternate option prefixes', () => {
    expect(renderCliArgs({ host: 'localhost', port: 502 }, '--config.')).toEqual([
      '--config.host="localhost"',
      '--config.port=502',
    ]);
  });
});

describe('CommandLineExample component', () => {
  it('shows placeholder when no command selected', () => {
    render(<ConfigProvider><CommandLineExample driverId="drv" command={null} params={{}} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-placeholder')).toBeDefined();
  });

  it('shows args only (no driverId) when command selected', () => {
    render(<ConfigProvider><CommandLineExample driverId="calc" command="add" params={{ a: 1 }} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-text')).toBeDefined();
    expect(screen.getByTestId('cmdline-text').textContent).toBe('--cmd=add --a=1');
  });

  it('renders label above command line', () => {
    render(<ConfigProvider><CommandLineExample driverId="calc" command="add" params={{}} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-label')).toBeDefined();
  });

  it('renders copy button', () => {
    render(<ConfigProvider><CommandLineExample driverId="calc" command="add" params={{}} /></ConfigProvider>);
    expect(screen.getByTestId('cmdline-copy')).toBeDefined();
  });
});
