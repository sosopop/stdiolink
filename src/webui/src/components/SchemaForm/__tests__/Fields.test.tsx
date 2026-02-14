import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { StringField } from '../fields/StringField';
import { NumberField } from '../fields/NumberField';
import { BoolField } from '../fields/BoolField';
import { EnumField } from '../fields/EnumField';
import { AnyField } from '../fields/AnyField';
import { ArrayField } from '../fields/ArrayField';

describe('StringField', () => {
  it('renders with value', () => {
    render(<ConfigProvider><StringField field={{ name: 'host', type: 'string' }} value="localhost" onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByDisplayValue('localhost')).toBeDefined();
  });

  it('calls onChange on input', () => {
    const onChange = vi.fn();
    render(<ConfigProvider><StringField field={{ name: 'host', type: 'string' }} value="" onChange={onChange} /></ConfigProvider>);
    fireEvent.change(screen.getByTestId('input-host'), { target: { value: 'new' } });
    expect(onChange).toHaveBeenCalledWith('new');
  });

  it('shows placeholder', () => {
    render(<ConfigProvider><StringField field={{ name: 'host', type: 'string', ui: { placeholder: 'Enter host' } }} value="" onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByPlaceholderText('Enter host')).toBeDefined();
  });
});

describe('NumberField', () => {
  it('renders with min/max', () => {
    render(<ConfigProvider><NumberField field={{ name: 'port', type: 'int', min: 1, max: 65535 }} value={8080} onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('input-port')).toBeDefined();
  });

  it('renders with step', () => {
    render(<ConfigProvider><NumberField field={{ name: 'rate', type: 'double', ui: { step: 0.1 } }} value={1.5} onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('input-rate')).toBeDefined();
  });
});

describe('BoolField', () => {
  it('renders switch', () => {
    render(<ConfigProvider><BoolField field={{ name: 'debug', type: 'bool' }} value={true} onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('input-debug')).toBeDefined();
  });
});

describe('EnumField', () => {
  it('renders select with options', () => {
    render(<ConfigProvider><EnumField field={{ name: 'mode', type: 'enum', enum: ['fast', 'slow', 'auto'] }} value="fast" onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('input-mode')).toBeDefined();
  });
});

describe('AnyField', () => {
  it('renders textarea for JSON', () => {
    render(<ConfigProvider><AnyField field={{ name: 'data', type: 'any' }} value={{ key: 'val' }} onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('input-data')).toBeDefined();
  });
});

describe('ArrayField', () => {
  it('renders add button', () => {
    render(<ConfigProvider><ArrayField field={{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' } }} value={[]} onChange={vi.fn()} /></ConfigProvider>);
    expect(screen.getByTestId('add-item-tags')).toBeDefined();
  });

  it('adds item on click', () => {
    const onChange = vi.fn();
    render(<ConfigProvider><ArrayField field={{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' } }} value={['a']} onChange={onChange} /></ConfigProvider>);
    fireEvent.click(screen.getByTestId('add-item-tags'));
    expect(onChange).toHaveBeenCalledWith(['a', '']);
  });

  it('removes item on click', () => {
    const onChange = vi.fn();
    render(<ConfigProvider><ArrayField field={{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' } }} value={['a', 'b']} onChange={onChange} /></ConfigProvider>);
    fireEvent.click(screen.getByTestId('remove-item-0'));
    expect(onChange).toHaveBeenCalledWith(['b']);
  });

  it('disables remove when at minItems', () => {
    render(<ConfigProvider><ArrayField field={{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' }, minItems: 1 }} value={['a']} onChange={vi.fn()} /></ConfigProvider>);
    const btn = screen.getByTestId('remove-item-0');
    expect(btn.closest('button')?.disabled).toBe(true);
  });

  it('disables add when at maxItems', () => {
    render(<ConfigProvider><ArrayField field={{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' }, maxItems: 2 }} value={['a', 'b']} onChange={vi.fn()} /></ConfigProvider>);
    const btn = screen.getByTestId('add-item-tags');
    expect(btn.closest('button')?.disabled).toBe(true);
  });
});
