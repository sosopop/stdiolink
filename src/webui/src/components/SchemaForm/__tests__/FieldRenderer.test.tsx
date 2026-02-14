import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { FieldRenderer } from '../FieldRenderer';
import type { FieldMeta } from '@/types/service';

function renderField(field: FieldMeta, value: unknown = undefined) {
  const onChange = vi.fn();
  const result = render(
    <ConfigProvider>
      <FieldRenderer field={field} value={value} onChange={onChange} />
    </ConfigProvider>,
  );
  return { ...result, onChange };
}

describe('FieldRenderer', () => {
  it('renders StringField for string type', () => {
    renderField({ name: 'host', type: 'string' });
    expect(screen.getByTestId('field-host')).toBeDefined();
  });

  it('renders NumberField for int type', () => {
    renderField({ name: 'port', type: 'int' });
    expect(screen.getByTestId('field-port')).toBeDefined();
  });

  it('renders NumberField for double type', () => {
    renderField({ name: 'rate', type: 'double' });
    expect(screen.getByTestId('field-rate')).toBeDefined();
  });

  it('renders BoolField for bool type', () => {
    renderField({ name: 'debug', type: 'bool' });
    expect(screen.getByTestId('field-debug')).toBeDefined();
  });

  it('renders EnumField for enum type', () => {
    renderField({ name: 'mode', type: 'enum', enum: ['a', 'b'] });
    expect(screen.getByTestId('field-mode')).toBeDefined();
  });

  it('renders ObjectField for object type', () => {
    renderField({ name: 'db', type: 'object', fields: [{ name: 'host', type: 'string' }] });
    expect(screen.getByTestId('field-db')).toBeDefined();
  });

  it('renders ArrayField for array type', () => {
    renderField({ name: 'items', type: 'array', items: { name: 'item', type: 'string' } });
    expect(screen.getByTestId('field-items')).toBeDefined();
  });

  it('renders AnyField for unknown type', () => {
    renderField({ name: 'data', type: 'any' });
    expect(screen.getByTestId('field-data')).toBeDefined();
  });
});
