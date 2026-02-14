import React from 'react';
import { Button } from 'antd';
import { ExportOutlined } from '@ant-design/icons';
import type { DriverMeta } from '@/types/driver';

interface ExportMetaButtonProps {
  driverId: string;
  meta: DriverMeta | null;
}

export const ExportMetaButton: React.FC<ExportMetaButtonProps> = ({ driverId, meta }) => {
  const handleExport = () => {
    if (!meta) return;
    const json = JSON.stringify(meta, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${driverId}_meta.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <Button
      icon={<ExportOutlined />}
      onClick={handleExport}
      disabled={!meta}
      data-testid="export-meta-btn"
    >
      Export Meta JSON
    </Button>
  );
};
