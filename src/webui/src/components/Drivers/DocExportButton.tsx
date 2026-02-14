import React from 'react';
import { Dropdown, Button, message } from 'antd';
import { DownloadOutlined } from '@ant-design/icons';
import { useDriversStore } from '@/stores/useDriversStore';

interface DocExportButtonProps {
  driverId: string;
}

function downloadBlob(content: string, filename: string) {
  const blob = new Blob([content], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export const DocExportButton: React.FC<DocExportButtonProps> = ({ driverId }) => {
  const { fetchDriverDocs } = useDriversStore();

  const handleExport = async (format: string) => {
    try {
      const content = await fetchDriverDocs(driverId, format);
      const ext = format === 'markdown' ? 'md' : format === 'typescript' ? 'd.ts' : 'html';
      downloadBlob(content, `${driverId}.${ext}`);
    } catch {
      message.error('Failed to export document');
    }
  };

  const items = [
    { key: 'markdown', label: 'Markdown (.md)' },
    { key: 'html', label: 'HTML (.html)' },
    { key: 'typescript', label: 'TypeScript (.d.ts)' },
  ];

  return (
    <Dropdown
      menu={{ items, onClick: ({ key }) => handleExport(key) }}
      data-testid="doc-export-dropdown"
    >
      <Button icon={<DownloadOutlined />} data-testid="doc-export-btn">
        Export Docs
      </Button>
    </Dropdown>
  );
};
