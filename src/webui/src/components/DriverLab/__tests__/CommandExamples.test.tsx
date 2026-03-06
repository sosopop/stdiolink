import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { CommandExamples } from '../CommandExamples';

describe('CommandExamples', () => {
  it('renders all valid examples with mode metadata', () => {
    render(
      <ConfigProvider>
        <CommandExamples
          examples={[
            { description: '读取示例', mode: 'stdio', params: { host: '127.0.0.1', port: 502 } },
            { description: '写入示例', mode: 'console', params: { address: 1, value: 10 } },
          ]}
          onApply={vi.fn()}
        />
      </ConfigProvider>,
    );

    expect(screen.getByTestId('command-examples')).toBeDefined();
    expect(screen.getByTestId('example-item-0')).toBeDefined();
    expect(screen.getByTestId('example-item-1')).toBeDefined();
    expect(screen.getByTestId('example-params-0').textContent).toContain('"host":"127.0.0.1"');
    expect(screen.getByTestId('example-description-0').textContent).toContain('读取示例');
    expect(screen.getByTestId('example-mode-0').textContent).toContain('stdio');
    expect(screen.getByTestId('example-mode-1').textContent).toContain('console');
  });

  it('applies example params on click', () => {
    const onApply = vi.fn();
    render(
      <ConfigProvider>
        <CommandExamples
          examples={[
            { description: '写入示例', mode: 'console', params: { address: 1, value: 10 } },
          ]}
          onApply={onApply}
        />
      </ConfigProvider>,
    );

    fireEvent.click(screen.getByTestId('apply-example-0'));
    expect(onApply).toHaveBeenCalledWith({ address: 1, value: 10 });
  });

  it('does not render for empty examples', () => {
    const { container } = render(
      <ConfigProvider>
        <CommandExamples examples={[]} onApply={vi.fn()} />
      </ConfigProvider>,
    );

    expect(container.textContent).toBe('');
  });
});
