import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { EnumValuesEditor } from '../EnumValuesEditor';

const wrap = (ui: React.ReactElement) => render(<ConfigProvider>{ui}</ConfigProvider>);

describe('EnumValuesEditor', () => {
  it('renders value list', () => {
    const onChange = vi.fn();
    wrap(<EnumValuesEditor values={['a', 'b', 'c']} onChange={onChange} />);
    expect(screen.getByTestId('enum-value-0')).toHaveValue('a');
    expect(screen.getByTestId('enum-value-1')).toHaveValue('b');
    expect(screen.getByTestId('enum-value-2')).toHaveValue('c');
  });

  it('adds option', () => {
    const onChange = vi.fn();
    wrap(<EnumValuesEditor values={['a']} onChange={onChange} />);
    fireEvent.click(screen.getByTestId('enum-add-btn'));
    expect(onChange).toHaveBeenCalledWith(['a', '']);
  });

  it('removes option', () => {
    const onChange = vi.fn();
    wrap(<EnumValuesEditor values={['a', 'b']} onChange={onChange} />);
    fireEvent.click(screen.getByTestId('enum-remove-0'));
    expect(onChange).toHaveBeenCalledWith(['b']);
  });

  it('edits option', () => {
    const onChange = vi.fn();
    wrap(<EnumValuesEditor values={['a', 'b']} onChange={onChange} />);
    fireEvent.change(screen.getByTestId('enum-value-0'), { target: { value: 'x' } });
    expect(onChange).toHaveBeenCalledWith(['x', 'b']);
  });

  it('renders empty state with add button', () => {
    const onChange = vi.fn();
    wrap(<EnumValuesEditor values={[]} onChange={onChange} />);
    expect(screen.getByTestId('enum-add-btn')).toBeInTheDocument();
  });
});
