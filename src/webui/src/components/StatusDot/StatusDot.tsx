import React from 'react';

export type StatusDotVariant = 'running' | 'stopped' | 'error';

interface StatusDotProps {
  status: StatusDotVariant;
  size?: number;
}

export const StatusDot: React.FC<StatusDotProps> = ({ status, size = 8 }) => {
  const variantClass = 
    status === 'running' ? 'status-dot--running' : 
    status === 'error' ? 'status-dot--error' : 
    'status-dot--stopped';

  return (
    <span
      className={`status-dot ${variantClass}`}
      data-testid="status-dot"
      data-status={status}
      style={{ width: size, height: size }}
    />
  );
};
