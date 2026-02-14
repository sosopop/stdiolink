import React, { useState, useEffect, useCallback } from 'react';
import { useTranslation } from 'react-i18next';
import { Spin, message, Button } from 'antd';
import { PlusOutlined } from '@ant-design/icons';
import { FileTree } from '@/components/FileTree/FileTree';
import { MonacoEditor } from '@/components/CodeEditor/MonacoEditor';
import { servicesApi } from '@/api/services';
import type { ServiceFile } from '@/types/service';

interface ServiceFilesProps {
  serviceId: string;
}

export const ServiceFiles: React.FC<ServiceFilesProps> = ({ serviceId }) => {
  const { t } = useTranslation();
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
      message.error(t('services.files.load_fail'));
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
      message.error(t('services.files.read_fail'));
    } finally {
      setLoading(false);
    }
  };

  const handleSave = async () => {
    if (!selectedPath) return;
    try {
      await servicesApi.fileWrite(serviceId, selectedPath, content);
      setModified(false);
      message.success(t('services.files.save_success'));
    } catch {
      message.error(t('services.files.save_fail'));
    }
  };

  const handleCreate = async (path: string) => {
    try {
      await servicesApi.fileCreate(serviceId, path, '');
      await loadFiles();
      message.success(t('services.files.create_success'));
    } catch {
      message.error(t('services.files.create_fail'));
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
      message.success(t('services.files.delete_success'));
    } catch {
      message.error(t('services.files.delete_fail'));
    }
  };

  return (
    <div data-testid="service-files" style={{ display: 'flex', gap: 16, height: 'calc(100vh - 240px)', minHeight: 500 }}>
      <div className="glass-panel" style={{ width: 260, flexShrink: 0, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        <div style={{
          padding: '12px 16px',
          borderBottom: '1px solid var(--surface-border)',
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center'
        }}>
          <span style={{ fontWeight: 600, color: 'var(--text-secondary)', fontSize: 12, textTransform: 'uppercase', letterSpacing: '0.5px' }}>
            {t('services.files.explorer')}
          </span>
          <Button
            type="text"
            size="small"
            icon={<PlusOutlined />}
            style={{ color: 'var(--text-secondary)' }}
            onClick={() => handleCreate('new_file.js')}
            data-testid="create-file-btn"
          />
        </div>
        <div style={{ flex: 1, overflow: 'auto', padding: '8px 5px' }}>
          <FileTree
            files={files}
            selectedPath={selectedPath}
            onSelect={handleSelect}
            onDeleteFile={handleDelete}
          />
        </div>
      </div>
      <div className="glass-panel" style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
        {loading ? (
          <div style={{ display: 'grid', placeItems: 'center', height: '100%' }}>
            <Spin data-testid="file-loading" />
          </div>
        ) : selectedPath ? (
          <>
            <div style={{
              padding: '8px 16px',
              borderBottom: '1px solid var(--surface-border)',
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
              background: 'rgba(0,0,0,0.2)'
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: 13, color: 'var(--text-primary)' }}>{selectedPath}</span>
                {modified && <span style={{ color: 'var(--brand-warning)', fontSize: 12, display: 'flex', alignItems: 'center', gap: 4 }}>
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'currentColor' }}></span>
                  {t('services.files.unsaved')}
                </span>}
              </div>
              <div style={{ display: 'flex', gap: 8 }}>
                <Button
                  size="small"
                  onClick={() => handleSelect(selectedPath)}
                  disabled={!modified}
                  type="text"
                >
                  {t('services.files.reset')}
                </Button>
                <Button
                  type="primary"
                  size="small"
                  onClick={handleSave}
                  loading={loading}
                  disabled={!modified}
                  style={{
                    boxShadow: modified ? '0 0 10px rgba(99, 102, 241, 0.4)' : 'none'
                  }}
                >
                  {t('services.files.save_changes')}
                </Button>
              </div>
            </div>
            <div style={{ flex: 1, position: 'relative' }}>
              <div style={{ position: 'absolute', inset: 0 }}>
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
            </div>
          </>
        ) : (
          <div data-testid="no-file-selected" style={{ display: 'grid', placeItems: 'center', height: '100%', color: 'var(--text-tertiary)' }}>
            <div style={{ textAlign: 'center' }}>
              <div style={{ fontSize: 48, marginBottom: 16, opacity: 0.2 }}>📄</div>
              <div>{t('services.files.no_file')}</div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
