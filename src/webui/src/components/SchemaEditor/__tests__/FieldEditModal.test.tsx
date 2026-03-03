import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { FieldEditModal } from '../FieldEditModal';
import { nodesToSchema, type SchemaNode } from '@/utils/schemaPath';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

function selectValue(testId: string, optionLabel: string) {
  const root = screen.getByTestId(testId) as HTMLElement;
  const combobox = (root.querySelector('[role="combobox"]') as HTMLElement | null) ?? root;
  fireEvent.mouseDown(combobox);

  const options = screen.queryAllByRole('option', { name: optionLabel });
  if (options.length > 0) {
    fireEvent.click(options[options.length - 1]);
    return;
  }

  const byText = screen.queryAllByText(optionLabel);
  if (byText.length > 0) {
    fireEvent.click(byText[byText.length - 1]);
    return;
  }

  throw new Error(`option not found: ${optionLabel}`);
}

describe('FieldEditModal', () => {
  const defaultProps = {
    visible: true,
    field: null as SchemaNode | null,
    existingNames: [] as string[],
    onSave: vi.fn(),
    onCancel: vi.fn(),
  };

  it('shows "Add Field" title for new field', () => {
    wrap(<FieldEditModal {...defaultProps} />);
    expect(screen.getByText('Add Field')).toBeInTheDocument();
  });

  it('shows "Edit Field" title for existing field', () => {
    const field: SchemaNode = { name: 'host', descriptor: { type: 'string', description: 'Host' } };
    wrap(<FieldEditModal {...defaultProps} field={field} />);
    expect(screen.getByText('Edit Field')).toBeInTheDocument();
  });

  it('pre-fills form for existing field', () => {
    const field: SchemaNode = { name: 'host', descriptor: { type: 'string', description: 'Host addr' } };
    wrap(<FieldEditModal {...defaultProps} field={field} />);
    expect(screen.getByTestId('field-name-input')).toHaveValue('host');
    expect(screen.getByTestId('field-description-input')).toHaveValue('Host addr');
  });

  it('shows error for empty name', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} onSave={onSave} />);
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      expect(screen.getByText('Field name is required')).toBeInTheDocument();
    });
    expect(onSave).not.toHaveBeenCalled();
  });

  it('shows error for duplicate name', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} existingNames={['host']} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-name-input'), { target: { value: 'host' } });
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      expect(screen.getByText('Field name already exists')).toBeInTheDocument();
    });
    expect(onSave).not.toHaveBeenCalled();
  });

  it('calls onSave with valid field', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-name-input'), { target: { value: 'port' } });
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      expect(onSave).toHaveBeenCalledWith(
        expect.objectContaining({ name: 'port' }),
      );
    });
  });

  it('calls onCancel', () => {
    const onCancel = vi.fn();
    wrap(<FieldEditModal {...defaultProps} onCancel={onCancel} />);
    fireEvent.click(screen.getByText('Cancel'));
    expect(onCancel).toHaveBeenCalled();
  });

  // R03
  it('keeps hidden items when editing array field', async () => {
    const field: SchemaNode = {
      name: 'radars',
      descriptor: {
        type: 'array',
        items: { type: 'object', fields: { ip: { type: 'string' } } },
      },
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-description-input'), {
      target: { value: 'Radar list' },
    });
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      expect(onSave).toHaveBeenCalled();
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.items).toBeDefined();
      expect(saved.descriptor.items.fields.ip).toBeDefined();
      expect(saved.descriptor.description).toBe('Radar list');
    });
  });

  // R04
  it('keeps requiredKeys and additionalProperties when editing object field', async () => {
    const field: SchemaNode = {
      name: 'server',
      descriptor: {
        type: 'object',
        requiredKeys: ['host', 'port'],
        additionalProperties: false,
      },
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-description-input'), {
      target: { value: 'Server config' },
    });
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.requiredKeys).toEqual(['host', 'port']);
      expect(saved.descriptor.additionalProperties).toBe(false);
    });
  });

  // R05
  it('rejects field name containing dot', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-name-input'), { target: { value: 'radar.port' } });
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      expect(screen.getByText("Field name cannot contain '.'")).toBeInTheDocument();
    });
    expect(onSave).not.toHaveBeenCalled();
  });

  // R12
  it('starts from empty descriptor in create mode', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={null} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-name-input'), { target: { value: 'port' } });
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.items).toBeUndefined();
      expect(saved.descriptor.requiredKeys).toBeUndefined();
    });
  });

  // R13
  it('cleans items when type changes from array to string', async () => {
    const field: SchemaNode = {
      name: 'tags',
      descriptor: {
        type: 'array',
        items: { type: 'object', fields: { id: { type: 'string' } } },
      },
      children: [{ name: 'id', descriptor: { type: 'string' } }],
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    selectValue('field-type-select', 'string');
    fireEvent.click(screen.getByText('OK'));
    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.type).toBe('string');
      expect(saved.descriptor.items).toBeUndefined();
      expect(saved.children).toBeUndefined();
      const back = nodesToSchema([saved]);
      expect(back.tags.fields).toBeUndefined();
      expect(back.tags.items).toBeUndefined();
    });
  });

  it('cleans object fields and children when type changes from object to string', async () => {
    const field: SchemaNode = {
      name: 'server',
      descriptor: {
        type: 'object',
        fields: { host: { type: 'string' } },
        requiredKeys: ['host'],
        additionalProperties: false,
      },
      children: [{ name: 'host', descriptor: { type: 'string' } }],
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    selectValue('field-type-select', 'string');
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.type).toBe('string');
      expect(saved.descriptor.fields).toBeUndefined();
      expect(saved.descriptor.requiredKeys).toBeUndefined();
      expect(saved.descriptor.additionalProperties).toBeUndefined();
      expect(saved.children).toBeUndefined();
      const back = nodesToSchema([saved]);
      expect(back.server.fields).toBeUndefined();
      expect(back.server.items).toBeUndefined();
    });
  });

  // R14
  it('writes descriptor.items.type when type=array and items type is selected', async () => {
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={null} onSave={onSave} />);
    fireEvent.change(screen.getByTestId('field-name-input'), { target: { value: 'radars' } });
    selectValue('field-type-select', 'array');
    selectValue('field-items-type-select', 'object');
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.type).toBe('array');
      expect(saved.descriptor.items?.type).toBe('object');
    });
  });

  it('cleans object-only items keys when items type changes to string', async () => {
    const field: SchemaNode = {
      name: 'radars',
      descriptor: {
        type: 'array',
        items: {
          type: 'object',
          fields: { id: { type: 'string' } },
          requiredKeys: ['id'],
          additionalProperties: false,
        },
      },
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    selectValue('field-items-type-select', 'string');
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.type).toBe('array');
      expect(saved.descriptor.items?.type).toBe('string');
      expect(saved.descriptor.items?.fields).toBeUndefined();
      expect(saved.descriptor.items?.requiredKeys).toBeUndefined();
      expect(saved.descriptor.items?.additionalProperties).toBeUndefined();
    });
  });

  it('keeps object default value unchanged when user does not edit default input', async () => {
    const field: SchemaNode = {
      name: 'limits',
      descriptor: {
        type: 'any',
        default: { max: 3, enabled: true },
      },
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.default).toEqual({ max: 3, enabled: true });
    });
  });

  it('keeps empty-string default when default input is untouched', async () => {
    const field: SchemaNode = {
      name: 'id',
      descriptor: {
        type: 'string',
        default: '',
      },
    };
    const onSave = vi.fn();
    wrap(<FieldEditModal {...defaultProps} field={field} onSave={onSave} />);
    fireEvent.click(screen.getByText('OK'));

    await waitFor(() => {
      const saved = onSave.mock.calls[0][0];
      expect(saved.descriptor.default).toBe('');
    });
  });
});
