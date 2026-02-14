import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { FieldEditModal } from '../FieldEditModal';
import type { SchemaNode } from '@/utils/schemaPath';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

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
});
