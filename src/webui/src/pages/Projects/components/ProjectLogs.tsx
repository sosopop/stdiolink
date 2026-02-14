import React, { useEffect, useState, useCallback } from 'react';
import { Button } from 'antd';
import { ReloadOutlined } from '@ant-design/icons';
import { LogViewer } from '@/components/LogViewer/LogViewer';
import { projectsApi } from '@/api/projects';

interface ProjectLogsProps {
  projectId: string;
}

export const ProjectLogs: React.FC<ProjectLogsProps> = ({ projectId }) => {
  const [lines, setLines] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);

  const loadLogs = useCallback(async () => {
    setLoading(true);
    try {
      const data = await projectsApi.logs(projectId, { lines: 500 });
      setLines(data.lines);
    } catch {
      // silently fail
    } finally {
      setLoading(false);
    }
  }, [projectId]);

  useEffect(() => {
    loadLogs();
  }, [loadLogs]);

  return (
    <div data-testid="project-logs">
      <Button
        icon={<ReloadOutlined />}
        onClick={loadLogs}
        loading={loading}
        style={{ marginBottom: 8 }}
        data-testid="refresh-logs-btn"
      >
        Refresh
      </Button>
      <LogViewer lines={lines} loading={loading} />
    </div>
  );
};
