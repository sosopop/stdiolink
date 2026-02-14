import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { SchemaForm } from '../SchemaForm';
import type { FieldMeta } from '@/types/service';

function renderForm(schema: FieldMeta[], value: Record<string, unknown> = {}, props: Record<string, unknown> = {}) {
  const onChange = vi.fn();
  const result = render(
    <ConfigProvider>
      <SchemaForm schema={schema} value={value} onChange={onChange} {...props} />
    </ConfigProvider>,
  );
  return { ...result, onChange };
}

describe('SchemaForm', () => {
  it('renders string field as Input', () => {
    renderForm([{ name: 'host', type: 'string' }]);
    expect(screen.getByTestId('field-host')).toBeDefined();
    expect(screen.getByTestId('input-host')).toBeDefined();
  });

  it('renders int field as InputNumber', () => {
    renderForm([{ name: 'port', type: 'int' }]);
    expect(screen.getByTestId('field-port')).toBeDefined();
    expect(screen.getByTestId('input-port')).toBeDefined();
  });

  it('renders bool field as Switch', () => {
    renderForm([{ name: 'debug', type: 'bool' }]);
    expect(screen.getByTestId('field-debug')).toBeDefined();
    expect(screen.getByTestId('input-debug')).toBeDefined();
  });

  it('renders enum field as Select', () => {
    renderForm([{ name: 'mode', type: 'enum', enum: ['fast', 'slow'] }]);
    expect(screen.getByTestId('field-mode')).toBeDefined();
    expect(screen.getByTestId('input-mode')).toBeDefined();
  });

  it('renders object field with nested fields', () => {
    renderForm([{
      name: 'database',
      type: 'object',
      fields: [
        { name: 'host', type: 'string' },
        { name: 'port', type: 'int' },
      ],
    }]);
    expect(screen.getByTestId('field-database')).toBeDefined();
    expect(screen.getByTestId('object-fields-database')).toBeDefined();
  });

  it('renders array field with add button', () => {
    renderForm([{ name: 'tags', type: 'array', items: { name: 'item', type: 'string' } }]);
    expect(screen.getByTestId('field-tags')).toBeDefined();
    expect(screen.getByTestId('add-item-tags')).toBeDefined();
  });

  it('renders required label', () => {
    renderForm([{ name: 'host', type: 'string', required: true }]);
    expect(screen.getByTestId('field-host')).toBeDefined();
  });

  it('renders with default values', () => {
    renderForm([{ name: 'host', type: 'string' }], { host: 'localhost' });
    expect(screen.getByDisplayValue('localhost')).toBeDefined();
  });

  it('renders placeholder', () => {
    renderForm([{ name: 'host', type: 'string', ui: { placeholder: 'Enter host' } }]);
    expect(screen.getByPlaceholderText('Enter host')).toBeDefined();
  });

  it('renders error message', () => {
    renderForm([{ name: 'host', type: 'string' }], {}, { errors: { host: 'Required field' } });
    expect(screen.getByText('Required field')).toBeDefined();
  });

  it('renders grouped fields in Collapse', () => {
    renderForm([
      { name: 'host', type: 'string', ui: { group: 'Network' } },
      { name: 'port', type: 'int', ui: { group: 'Network' } },
    ]);
    expect(screen.getByTestId('group-Network')).toBeDefined();
  });

  it('renders advanced fields in collapsed section', () => {
    renderForm([
      { name: 'host', type: 'string' },
      { name: 'debug', type: 'bool', ui: { advanced: true } },
    ]);
    expect(screen.getByText('Advanced Options')).toBeDefined();
  });

  it('calls onChange on value change', () => {
    const { onChange } = renderForm([{ name: 'host', type: 'string' }], { host: '' });
    fireEvent.change(screen.getByTestId('input-host'), { target: { value: 'newhost' } });
    expect(onChange).toHaveBeenCalledWith({ host: 'newhost' });
  });
});
