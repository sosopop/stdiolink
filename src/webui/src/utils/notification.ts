import { message, notification } from 'antd';

export const notify = {
  success: (msg: string) => message.success(msg),
  error: (msg: string) => notification.error({ message: 'Error', description: msg }),
  warning: (msg: string) => message.warning(msg),
  info: (msg: string) => message.info(msg),
};
