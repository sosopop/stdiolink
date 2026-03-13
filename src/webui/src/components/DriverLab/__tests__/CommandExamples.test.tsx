import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import i18n from '@/i18n';
import { CommandExamples } from '../CommandExamples';

describe('CommandExamples', () => {
  it('renders examples with a generic title and without mode metadata', () => {
    const expectedTitle = i18n.t('driverlab.command.examples_title');
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
    expect(screen.getByTestId('example-description-0').textContent).toBe(expectedTitle);
    expect(screen.getByTestId('example-description-0').textContent).not.toContain('stdio');
    expect(screen.queryByTestId('example-mode-0')).toBeNull();
    expect(screen.queryByText('console')).toBeNull();
    expect(screen.getByTestId('example-scroll-0')).toHaveStyle({
      overflowX: 'auto',
      overflowY: 'hidden',
      maxWidth: '100%',
      width: '0px',
    });
    expect(screen.getByTestId('example-params-0')).toHaveStyle({
      whiteSpace: 'nowrap',
      minWidth: 'max-content',
    });
    expect(screen.getByTestId('toggle-wrap-0')).toBeDefined();
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

  it('toggles example wrapping on and off', () => {
    render(
      <ConfigProvider>
        <CommandExamples
          examples={[
            { description: '长示例', mode: 'stdio', params: { host: '127.0.0.1', port: 502, note: 'a long line to wrap' } },
          ]}
          onApply={vi.fn()}
        />
      </ConfigProvider>,
    );

    const toggle = screen.getByTestId('toggle-wrap-0');
    const scroll = screen.getByTestId('example-scroll-0');
    const params = screen.getByTestId('example-params-0');

    expect(toggle).toHaveAttribute('aria-label', i18n.t('driverlab.command.wrap_enable'));
    expect(scroll).toHaveStyle({ overflowX: 'auto' });
    expect(params).toHaveStyle({ whiteSpace: 'nowrap' });

    fireEvent.click(toggle);
    expect(screen.getByTestId('toggle-wrap-0')).toHaveAttribute('aria-label', i18n.t('driverlab.command.wrap_disable'));
    expect(screen.getByTestId('example-scroll-0')).toHaveStyle({ overflowX: 'hidden' });
    expect(screen.getByTestId('example-params-0')).toHaveStyle({ whiteSpace: 'pre-wrap' });
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
