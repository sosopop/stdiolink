import React from 'react';
import { Tree, Button, Popconfirm, Tag } from 'antd';
import { FileOutlined, DeleteOutlined } from '@ant-design/icons';
import type { ServiceFile } from '@/types/service';

const PROTECTED_FILES = ['manifest.json', 'index.js', 'config.schema.json'];

export interface FileTreeProps {
  files: ServiceFile[];
  selectedPath: string | null;
  onSelect: (path: string) => void;
  onDeleteFile?: (path: string) => void;
}

export const FileTree: React.FC<FileTreeProps> = ({
  files,
  selectedPath,
  onSelect,
  onDeleteFile,
}) => {
  const isProtected = (name: string) => PROTECTED_FILES.includes(name);

  const treeData = files.map((f) => ({
    key: f.path,
    title: (
      <span data-testid={`file-node-${f.name}`} style={{ display: 'flex', alignItems: 'center', padding: '2px 0' }}>
        <FileOutlined style={{ marginRight: 8, color: 'var(--brand-primary)', opacity: 0.8 }} />
        <span style={{ color: 'var(--text-primary)', fontSize: 13 }}>{f.name}</span>
        {isProtected(f.name) && (
          <Tag bordered={false} style={{ marginLeft: 8, fontSize: 10, background: 'rgba(99, 102, 241, 0.1)', color: 'var(--brand-primary)', borderRadius: 4, lineHeight: '16px', padding: '0 4px', border: 'none' }} data-testid={`protected-${f.name}`}>
            core
          </Tag>
        )}
      </span>
    ),
    isLeaf: true,
  }));

  return (
    <div data-testid="file-tree" style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
      <div style={{ flex: 1, overflow: 'auto' }}>
        <Tree
          treeData={treeData}
          selectedKeys={selectedPath ? [selectedPath] : []}
          onSelect={(keys) => {
            if (keys.length > 0) onSelect(keys[0] as string);
          }}
          blockNode
          virtual={false}
          style={{ background: 'transparent', color: 'var(--text-primary)' }}
        />
      </div>
      {selectedPath && onDeleteFile && (
        <div style={{
          padding: '12px 16px',
          borderTop: '1px solid var(--surface-border)',
          display: 'flex',
          justifyContent: 'flex-end',
          background: 'transparent'
        }}>
          <Popconfirm
            title="Delete this file?"
            onConfirm={() => onDeleteFile(selectedPath)}
            disabled={isProtected(files.find((f) => f.path === selectedPath)?.name ?? '')}
            okButtonProps={{ danger: true }}
          >
            <Button
              danger
              size="small"
              icon={<DeleteOutlined />}
              data-testid="delete-file-btn"
              disabled={isProtected(files.find((f) => f.path === selectedPath)?.name ?? '')}
              style={{
                background: 'rgba(239, 68, 68, 0.1)',
                border: '1px solid rgba(239, 68, 68, 0.2)',
                borderRadius: 4,
                display: 'flex',
                alignItems: 'center'
              }}
            >
              Delete
            </Button>
          </Popconfirm>
        </div>
      )}
    </div>
  );
};
