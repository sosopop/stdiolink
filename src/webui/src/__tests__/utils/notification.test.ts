import { describe, it, expect, vi } from 'vitest';
import { message, notification } from 'antd';
import { notify } from '@/utils/notification';

vi.mock('antd', () => ({
  message: { success: vi.fn(), warning: vi.fn(), info: vi.fn() },
  notification: { error: vi.fn() },
}));

describe('notify', () => {
  it('success() calls message.success', () => {
    notify.success('Done');
    expect(message.success).toHaveBeenCalledWith('Done');
  });

  it('error() calls notification.error', () => {
    notify.error('Something failed');
    expect(notification.error).toHaveBeenCalledWith({
      message: 'Error',
      description: 'Something failed',
    });
  });

  it('warning() calls message.warning', () => {
    notify.warning('Careful');
    expect(message.warning).toHaveBeenCalledWith('Careful');
  });

  it('info() calls message.info', () => {
    notify.info('FYI');
    expect(message.info).toHaveBeenCalledWith('FYI');
  });
});
