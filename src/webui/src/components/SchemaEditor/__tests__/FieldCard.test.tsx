import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { FieldCard } from '../FieldCard';
import type { SchemaNode } from '@/utils/schemaPath';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

const stringField: SchemaNode = {
  name: 'host',
  descriptor: {
    type: 'string',
    required: true,
    description: 'Host address',
    default: 'localhost',
    constraints: { minLength: 1, maxLength: 255, pattern: '^[a-z]+$' },
  },
};

const intField: SchemaNode = {
  name: 'port',
  descriptor: { type: 'int', constraints: { min: 1, max: 65535 } },
};

const objectField: SchemaNode = {
  name: 'options',
  descriptor: { type: 'object' },
  children: [
    { name: 'timeout', descriptor: { type: 'int', default: 30 } },
    { name: 'retries', descriptor: { type: 'int', default: 3 } },
  ],
};

const enumField: SchemaNode = {
  name: 'mode',
  descriptor: { type: 'enum', constraints: { enumValues: ['fast', 'slow'] } },
};

describe('FieldCard', () => {
  const defaultProps = {
    level: 0,
    isFirst: false,
    isLast: false,
    onEdit: vi.fn(),
    onDelete: vi.fn(),
    onMove: vi.fn(),
    onAddChild: vi.fn(),
  };

  it('renders field summary', () => {
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" />);
    expect(screen.getByText('host')).toBeInTheDocument();
    expect(screen.getByText('string')).toBeInTheDocument();
    expect(screen.getByText('required')).toBeInTheDocument();
    expect(screen.getByText('Host address')).toBeInTheDocument();
  });

  it('shows default value', () => {
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" />);
    expect(screen.getByText(/Default:.*"localhost"/)).toBeInTheDocument();
  });

  it('shows constraints for string type', () => {
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" />);
    expect(screen.getByText(/minLength=1/)).toBeInTheDocument();
    expect(screen.getByText(/pattern=/)).toBeInTheDocument();
  });

  it('shows constraints for int type', () => {
    wrap(<FieldCard {...defaultProps} field={intField} path="port" />);
    expect(screen.getByText(/min=1/)).toBeInTheDocument();
    expect(screen.getByText(/max=65535/)).toBeInTheDocument();
  });

  it('shows enum values', () => {
    wrap(<FieldCard {...defaultProps} field={enumField} path="mode" />);
    expect(screen.getByText(/values=\[fast, slow\]/)).toBeInTheDocument();
  });

  it('renders object with children expanded', () => {
    wrap(<FieldCard {...defaultProps} field={objectField} path="options" />);
    expect(screen.getByText('timeout')).toBeInTheDocument();
    expect(screen.getByText('retries')).toBeInTheDocument();
  });

  it('collapses object children on toggle', () => {
    wrap(<FieldCard {...defaultProps} field={objectField} path="options" />);
    fireEvent.click(screen.getByTestId('field-toggle-options'));
    expect(screen.getByText(/2 child field/)).toBeInTheDocument();
  });

  it('triggers onEdit', () => {
    const onEdit = vi.fn();
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" onEdit={onEdit} />);
    fireEvent.click(screen.getByTestId('field-edit-host'));
    expect(onEdit).toHaveBeenCalledWith('host');
  });

  it('triggers onDelete', () => {
    const onDelete = vi.fn();
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" onDelete={onDelete} />);
    fireEvent.click(screen.getByTestId('field-delete-host'));
    expect(onDelete).toHaveBeenCalledWith('host');
  });

  it('triggers onMove', () => {
    const onMove = vi.fn();
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" onMove={onMove} />);
    fireEvent.click(screen.getByTestId('field-up-host'));
    expect(onMove).toHaveBeenCalledWith('host', 'up');
  });

  it('disables up button when isFirst', () => {
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" isFirst={true} />);
    expect(screen.getByTestId('field-up-host')).toBeDisabled();
  });

  it('disables down button when isLast', () => {
    wrap(<FieldCard {...defaultProps} field={stringField} path="host" isLast={true} />);
    expect(screen.getByTestId('field-down-host')).toBeDisabled();
  });
});
