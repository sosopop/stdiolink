import React, { useState, useEffect, useCallback } from 'react';
import { Spin, message } from 'antd';
import { FileTree } from '@/components/FileTree/FileTree';
import { MonacoEditor } from '@/components/CodeEditor/MonacoEditor';
import { servicesApi } from '@/api/services';
import type { ServiceFile } from '@/types/service';

interface ServiceFilesProps {
  serviceId: string;
}

export const ServiceFiles: React.FC<ServiceFilesProps> = ({ serviceId }) => {
  const [files, setFiles] = useState<ServiceFile[]>([]);
  const [selectedPath, setSelectedPath] = useState<string | null>(null);
  const [content, setContent] = useState('');
  const [fileSize, setFileSize] = useState(0);
  const [modified, setModified] = useState(false);
  const [loading, setLoading] = useState(false);

  const loadFiles = useCallback(async () => {
    try {
      const data = await servicesApi.files(serviceId);
      setFiles(data.files);
    } catch {
      message.error('Failed to load files');
    }
  }, [serviceId]);

  useEffect(() => {
    loadFiles();
  }, [loadFiles]);

  const handleSelect = async (path: string) => {
    try {
      setLoading(true);
      const data = await servicesApi.fileRead(serviceId, path);
      setSelectedPath(path);
      setContent(data.content);
      setFileSize(data.size);
      setModified(false);
    } catch {
      message.error('Failed to read file');
    } finally {
      setLoading(false);
    }
  };

  const handleSave = async () => {
    if (!selectedPath) return;
    try {
      await servicesApi.fileWrite(serviceId, selectedPath, content);
      setModified(false);
      message.success('File saved');
    } catch {
      message.error('Failed to save file');
    }
  };

  const handleCreate = async (path: string) => {
    try {
      await servicesApi.fileCreate(serviceId, path, '');
      await loadFiles();
      message.success('File created');
    } catch {
      message.error('Failed to create file');
    }
  };

  const handleDelete = async (path: string) => {
    try {
      await servicesApi.fileDelete(serviceId, path);
      if (selectedPath === path) {
        setSelectedPath(null);
        setContent('');
      }
      await loadFiles();
      message.success('File deleted');
    } catch {
      message.error('Failed to delete file');
    }
  };

  return (
    <div data-testid="service-files" style={{ display: 'flex', gap: 16, height: '100%' }}>
      <div style={{ width: 240, flexShrink: 0 }}>
        <FileTree
          files={files}
          selectedPath={selectedPath}
          onSelect={handleSelect}
          onCreateFile={handleCreate}
          onDeleteFile={handleDelete}
        />
      </div>
      <div style={{ flex: 1, minWidth: 0 }}>
        {loading ? (
          <Spin data-testid="file-loading" />
        ) : selectedPath ? (
          <div>
            <div style={{ marginBottom: 8, fontSize: 12, color: '#888' }}>
              {selectedPath} {modified && <span data-testid="modified-marker">*</span>}
            </div>
            <MonacoEditor
              content={content}
              filePath={selectedPath}
              fileSize={fileSize}
              onChange={(v) => {
                setContent(v);
                setModified(true);
              }}
              onSave={handleSave}
            />
          </div>
        ) : (
          <div data-testid="no-file-selected" style={{ color: '#888', padding: 40, textAlign: 'center' }}>
            Select a file to edit
          </div>
        )}
      </div>
    </div>
  );
};
