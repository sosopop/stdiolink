import React from 'react';
import { Tree, Button, Popconfirm, Tag } from 'antd';
import { FileOutlined, PlusOutlined, DeleteOutlined } from '@ant-design/icons';
import type { ServiceFile } from '@/types/service';

const PROTECTED_FILES = ['manifest.json', 'index.js', 'config.schema.json'];

export interface FileTreeProps {
  files: ServiceFile[];
  selectedPath: string | null;
  onSelect: (path: string) => void;
  onCreateFile?: (path: string) => void;
  onDeleteFile?: (path: string) => void;
}

export const FileTree: React.FC<FileTreeProps> = ({
  files,
  selectedPath,
  onSelect,
  onCreateFile,
  onDeleteFile,
}) => {
  const isProtected = (name: string) => PROTECTED_FILES.includes(name);

  const treeData = files.map((f) => ({
    key: f.path,
    title: (
      <span data-testid={`file-node-${f.name}`}>
        <FileOutlined style={{ marginRight: 6 }} />
        {f.name}
        {isProtected(f.name) && (
          <Tag color="blue" style={{ marginLeft: 6, fontSize: 10 }} data-testid={`protected-${f.name}`}>
            core
          </Tag>
        )}
      </span>
    ),
    isLeaf: true,
  }));

  return (
    <div data-testid="file-tree">
      <div style={{ marginBottom: 8, display: 'flex', justifyContent: 'space-between' }}>
        <span style={{ fontWeight: 500 }}>Files</span>
        {onCreateFile && (
          <Button
            type="text"
            size="small"
            icon={<PlusOutlined />}
            data-testid="create-file-btn"
            onClick={() => onCreateFile('new_file.js')}
          />
        )}
      </div>
      <Tree
        treeData={treeData}
        selectedKeys={selectedPath ? [selectedPath] : []}
        onSelect={(keys) => {
          if (keys.length > 0) onSelect(keys[0] as string);
        }}
        blockNode
        virtual={false}
      />
      {selectedPath && onDeleteFile && (
        <div style={{ marginTop: 8 }}>
          <Popconfirm
            title="Delete this file?"
            onConfirm={() => onDeleteFile(selectedPath)}
            disabled={isProtected(files.find((f) => f.path === selectedPath)?.name ?? '')}
          >
            <Button
              danger
              size="small"
              icon={<DeleteOutlined />}
              data-testid="delete-file-btn"
              disabled={isProtected(files.find((f) => f.path === selectedPath)?.name ?? '')}
            >
              Delete
            </Button>
          </Popconfirm>
        </div>
      )}
    </div>
  );
};
