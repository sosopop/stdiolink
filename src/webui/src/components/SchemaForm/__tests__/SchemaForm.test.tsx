import React from 'react';
import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, within } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { SchemaForm } from '../SchemaForm';
import { ArrayField } from '../fields/ArrayField';
import { getDefaultItem } from '../utils/fieldDefaults';
import type { FieldMeta } from '@/types/service';
import i18n from '@/i18n';

function renderForm(schema: FieldMeta[], value: Record<string, unknown> = {}, props: Record<string, unknown> = {}) {
  const onChange = vi.fn();
  const result = render(
    <ConfigProvider>
      <SchemaForm schema={schema} value={value} onChange={onChange} {...props} />
    </ConfigProvider>,
  );
  return { ...result, onChange };
}

function renderControlledForm(schema: FieldMeta[], initialValue: Record<string, unknown> = {}) {
  const Wrapper = () => {
    const [value, setValue] = React.useState<Record<string, unknown>>(initialValue);
    return (
      <ConfigProvider>
        <SchemaForm schema={schema} value={value} onChange={setValue} />
      </ConfigProvider>
    );
  };

  return render(<Wrapper />);
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

  it('renders add-entry control for object additionalProperties field', () => {
    renderForm([{
      name: 'env',
      type: 'object',
      additionalProperties: true,
    }]);
    expect(screen.getByTestId('add-entry-env')).toBeDefined();
  });

  it('treats omitted additionalProperties as enabled for plain object fields', () => {
    renderForm([{
      name: 'env',
      type: 'object',
    }]);
    expect(screen.getByTestId('add-entry-env')).toBeDefined();
  });

  it('adds and edits object additionalProperties entries', () => {
    const { onChange } = renderForm([{
      name: 'env',
      type: 'object',
      additionalProperties: true,
    }], {
      env: {},
    });

    fireEvent.click(screen.getByTestId('add-entry-env'));
    expect(onChange).toHaveBeenCalledWith({ env: { key1: '' } });
  });

  it('updates existing object additionalProperties entry value', () => {
    const { onChange } = renderForm([{
      name: 'env',
      type: 'object',
      additionalProperties: true,
    }], {
      env: { PATH_EXT: 'tools' },
    });

    fireEvent.change(screen.getByTestId('object-entry-value-env-0'), { target: { value: 'bin' } });
    expect(onChange).toHaveBeenCalledWith({ env: { PATH_EXT: 'bin' } });
  });

  it('keeps focus while editing object additionalProperties key', () => {
    renderControlledForm([{
      name: 'env',
      type: 'object',
      additionalProperties: true,
    }], {
      env: { PATH_EXT: 'tools' },
    });

    const keyInput = screen.getByTestId('object-entry-key-env-0');
    keyInput.focus();
    fireEvent.change(keyInput, { target: { value: 'PATH_EXTRA' } });

    expect(screen.getByTestId('object-entry-key-env-0')).toHaveFocus();
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

describe('SchemaForm array<object> regression (M90)', () => {
  const radarField: FieldMeta = {
    name: 'radars',
    type: 'array',
    items: {
      name: 'radar',
      type: 'object',
      fields: [
        { name: 'id', type: 'string' },
        { name: 'port', type: 'int' },
      ],
    },
  };

  it('R_FE_01 deep initializes array<object> item', () => {
    const onChange = vi.fn();
    render(
      <ConfigProvider>
        <ArrayField field={radarField} value={[]} onChange={onChange} />
      </ConfigProvider>,
    );

    fireEvent.click(screen.getByTestId('add-item-radars'));
    expect(onChange).toHaveBeenCalledWith([{ id: '', port: 0 }]);
  });

  it('R_FE_01_NONOBJ keeps shallow default for array<string>', () => {
    const onChange = vi.fn();
    render(
      <ConfigProvider>
        <ArrayField
          field={{ name: 'tags', type: 'array', items: { name: 'tag', type: 'string' } }}
          value={[]}
          onChange={onChange}
        />
      </ConfigProvider>,
    );

    fireEvent.click(screen.getByTestId('add-item-tags'));
    expect(onChange).toHaveBeenCalledWith(['']);
  });

  it('R_FE_01_INT getDefaultItem returns 0 for int', () => {
    expect(getDefaultItem('int')).toBe(0);
  });

  it('R_FE_02 renders array item label with name and 1-based index', () => {
    render(
      <ConfigProvider>
        <ArrayField field={radarField} value={[{ id: 'r1', port: 2368 }]} onChange={vi.fn()} />
      </ConfigProvider>,
    );

    const firstItem = screen.getByTestId('array-item-0');
    expect(within(firstItem).getByText(/^radar$/i)).toBeDefined();
    expect(within(firstItem).getByText(/^1$/)).toBeDefined();
  });

  it('R_FE_03 propagates nested error path to array<object> sub-field', () => {
    render(
      <ConfigProvider>
        <ArrayField
          field={radarField}
          value={[{ id: 'r1', port: -1 }]}
          onChange={vi.fn()}
          errors={{ 'radars[0].port': 'port must be >= 0' }}
          basePath="radars"
        />
      </ConfigProvider>,
    );

    expect(screen.getByText('port must be >= 0')).toBeDefined();
  });

  // R18
  it('uses i18n text for add item button under zh locale', async () => {
    const previousLanguage = i18n.resolvedLanguage ?? i18n.language;
    await i18n.changeLanguage('zh');
    try {
      render(
        <ConfigProvider>
          <ArrayField field={radarField} value={[]} onChange={vi.fn()} />
        </ConfigProvider>,
      );
      expect(screen.getByTestId('add-item-radars')).toHaveTextContent('添加项');
    } finally {
      await i18n.changeLanguage(previousLanguage);
    }
  });
});

describe('getDefaultItem', () => {
  it('returns {} for object', () => expect(getDefaultItem('object')).toEqual({}));
  it('returns [] for array', () => expect(getDefaultItem('array')).toEqual([]));
  it('returns false for bool', () => expect(getDefaultItem('bool')).toBe(false));
  it('returns 0 for int', () => expect(getDefaultItem('int')).toBe(0));
  it('returns 0 for int64', () => expect(getDefaultItem('int64')).toBe(0));
  it('returns 0 for double', () => expect(getDefaultItem('double')).toBe(0));
  it('returns "" for string', () => expect(getDefaultItem('string')).toBe(''));
  it('returns "" for enum', () => expect(getDefaultItem('enum')).toBe(''));
  it('returns "" for undefined', () => expect(getDefaultItem(undefined)).toBe(''));
});
