import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ConstraintsSection } from '../ConstraintsSection';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

describe('ConstraintsSection', () => {
  it('renders string constraints', () => {
    wrap(<ConstraintsSection type="string" constraints={{ minLength: 1 }} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-string')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-minLength')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-maxLength')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-pattern')).toBeInTheDocument();
  });

  it('renders int constraints', () => {
    wrap(<ConstraintsSection type="int" constraints={{}} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-int')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-min')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-max')).toBeInTheDocument();
  });

  it('renders double constraints', () => {
    wrap(<ConstraintsSection type="double" constraints={{}} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-double')).toBeInTheDocument();
  });

  it('renders array constraints', () => {
    wrap(<ConstraintsSection type="array" constraints={{}} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-array')).toBeInTheDocument();
    expect(screen.getByTestId('constraint-minItems')).toBeInTheDocument();
  });

  it('renders no constraints for bool', () => {
    wrap(<ConstraintsSection type="bool" constraints={{}} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-none')).toBeInTheDocument();
  });

  it('renders enum constraints with EnumValuesEditor', () => {
    wrap(<ConstraintsSection type="enum" constraints={{ enumValues: ['a', 'b'] }} onChange={vi.fn()} />);
    expect(screen.getByTestId('constraints-enum')).toBeInTheDocument();
    expect(screen.getByTestId('enum-values-editor')).toBeInTheDocument();
  });
});
