import React, { useEffect } from 'react';
import { Spin, Alert, Button } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';
import { useDriversStore } from '@/stores/useDriversStore';
import styles from './DriverDocs.module.css';

interface DriverDocsProps {
  driverId: string;
}

export const DriverDocs: React.FC<DriverDocsProps> = ({ driverId }) => {
  const { docsMarkdown, docsLoading, error, fetchDriverDocs } = useDriversStore();

  useEffect(() => {
    fetchDriverDocs(driverId, 'markdown').catch(() => { });
  }, [driverId, fetchDriverDocs]);

  if (docsLoading) {
    return <Spin data-testid="docs-loading" />;
  }

  if (error && !docsMarkdown) {
    return (
      <div data-testid="docs-error">
        <Alert type="error" message={error} style={{ marginBottom: 8 }} />
        <Button icon={<ReloadOutlined />} onClick={() => fetchDriverDocs(driverId, 'markdown').catch(() => { })} data-testid="docs-retry">
          Retry
        </Button>
      </div>
    );
  }

  if (!docsMarkdown) {
    return <Alert type="info" message="No documentation available" data-testid="docs-empty" />;
  }

  return (
    <div data-testid="driver-docs" className={styles.driverDocs} style={{ background: 'var(--surface-hover)', borderRadius: 8, padding: 32 }}>
      <ReactMarkdown remarkPlugins={[remarkGfm]}>{docsMarkdown}</ReactMarkdown>
    </div>
  );
};
