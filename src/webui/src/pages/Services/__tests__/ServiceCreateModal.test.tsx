import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { ConfigProvider } from 'antd';
import { ServiceCreateModal } from '../components/ServiceCreateModal';

function renderModal(props: Partial<Parameters<typeof ServiceCreateModal>[0]> = {}) {
  const defaultProps = {
    open: true,
    onClose: vi.fn(),
    onCreate: vi.fn().mockResolvedValue(true),
    ...props,
  };
  return { ...render(<ConfigProvider><ServiceCreateModal {...defaultProps} /></ConfigProvider>), props: defaultProps };
}

describe('ServiceCreateModal', () => {
  beforeEach(() => vi.clearAllMocks());

  it('renders template selection on step 0', () => {
    renderModal();
    expect(screen.getByTestId('template-select')).toBeDefined();
    expect(screen.getByTestId('template-empty')).toBeDefined();
    expect(screen.getByTestId('template-basic')).toBeDefined();
    expect(screen.getByTestId('template-driver_demo')).toBeDefined();
  });

  it('shows ID input on step 1 after clicking Next', () => {
    renderModal();
    fireEvent.click(screen.getByTestId('modal-ok'));
    expect(screen.getByTestId('service-id-input')).toBeDefined();
  });

  it('shows ID validation error for invalid characters', () => {
    renderModal();
    fireEvent.click(screen.getByTestId('modal-ok')); // go to step 1
    const input = screen.getByTestId('service-id-input');
    fireEvent.change(input, { target: { value: 'bad id!' } });
    expect(screen.getByTestId('id-error')).toBeDefined();
  });

  it('disables OK when ID is empty on step 1', () => {
    renderModal();
    fireEvent.click(screen.getByTestId('modal-ok')); // go to step 1
    const okBtn = screen.getByTestId('modal-ok');
    expect(okBtn.closest('button')?.disabled).toBe(true);
  });

  it('shows confirm step and calls onCreate', async () => {
    const { props } = renderModal();
    // Step 0 -> 1
    fireEvent.click(screen.getByTestId('modal-ok'));
    // Enter ID
    fireEvent.change(screen.getByTestId('service-id-input'), { target: { value: 'my_svc' } });
    // Step 1 -> 2
    fireEvent.click(screen.getByTestId('modal-ok'));
    expect(screen.getByTestId('confirm-step')).toBeDefined();
    // Confirm
    fireEvent.click(screen.getByTestId('modal-ok'));
    await vi.waitFor(() => {
      expect(props.onCreate).toHaveBeenCalledWith({ id: 'my_svc', template: 'empty' });
    });
  });
});
