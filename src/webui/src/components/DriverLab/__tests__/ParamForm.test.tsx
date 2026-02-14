import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ParamForm } from '../ParamForm';
import type { FieldMeta } from '@/types/service';

function renderForm(params: FieldMeta[], values: Record<string, unknown> = {}) {
  const onChange = vi.fn();
  return {
    ...render(
      <ConfigProvider>
        <ParamForm params={params} values={values} onChange={onChange} />
      </ConfigProvider>,
    ),
    onChange,
  };
}

describe('ParamForm', () => {
  it('shows no-params message for empty params', () => {
    renderForm([]);
    expect(screen.getByTestId('no-params')).toBeDefined();
  });

  it('renders string field', () => {
    renderForm([{ name: 'host', type: 'string', required: true }]);
    expect(screen.getByTestId('field-host')).toBeDefined();
  });

  it('renders number field', () => {
    renderForm([{ name: 'port', type: 'int', required: true }]);
    expect(screen.getByTestId('field-port')).toBeDefined();
  });

  it('renders bool field', () => {
    renderForm([{ name: 'verbose', type: 'bool' }]);
    expect(screen.getByTestId('field-verbose')).toBeDefined();
  });

  it('renders enum field', () => {
    renderForm([{ name: 'mode', type: 'enum', enum: ['fast', 'slow'] }]);
    expect(screen.getByTestId('field-mode')).toBeDefined();
  });

  it('renders any field for object type', () => {
    renderForm([{ name: 'data', type: 'any' }]);
    expect(screen.getByTestId('field-data')).toBeDefined();
  });

  it('renders param form container', () => {
    renderForm([{ name: 'x', type: 'string' }]);
    expect(screen.getByTestId('param-form')).toBeDefined();
  });
});
