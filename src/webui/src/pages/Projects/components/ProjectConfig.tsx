import React, { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Card, Space, Typography, message } from 'antd';
import { CopyOutlined } from '@ant-design/icons';
import { SchemaForm } from '@/components/SchemaForm/SchemaForm';
import type { FieldMeta } from '@/types/service';
import {
  buildProjectCommandLines,
} from '@/utils/projectCommandLine';

interface ProjectConfigProps {
  projectId: string;
  config: Record<string, unknown>;
  schema: FieldMeta[];
  serviceDir?: string | null;
  dataRoot?: string | null;
  onSave: (config: Record<string, unknown>) => Promise<boolean>;
}

const { Text } = Typography;
const commandStyle: React.CSSProperties = {
  background: 'var(--surface-layer2, #1a1a2e)',
  padding: '12px 40px 12px 12px',
  borderRadius: 6,
  fontSize: 12,
  fontFamily: 'monospace',
  whiteSpace: 'pre-wrap',
  wordBreak: 'break-all',
  margin: 0,
};

export const ProjectConfig: React.FC<ProjectConfigProps> = ({
  projectId,
  config,
  schema,
  serviceDir = null,
  dataRoot = null,
  onSave,
}) => {
  const { t } = useTranslation();
  const [value, setValue] = useState<Record<string, unknown>>(config);
  const [saving, setSaving] = useState(false);
  const commandLines = buildProjectCommandLines({
    projectId,
    serviceDir,
    dataRoot,
    config,
  });
  const commandsReady = Boolean(commandLines.expanded && commandLines.configFile);

  useEffect(() => {
    setValue(config);
  }, [config]);

  const handleSave = async () => {
    setSaving(true);
    const ok = await onSave(value);
    if (ok) message.success(t('projects.config.save_success'));
    else message.error(t('projects.config.save_fail'));
    setSaving(false);
  };

  const handleCopy = async (command: string) => {
    try {
      await navigator.clipboard.writeText(command);
      message.success(t('projects.config.test_command_copied'));
    } catch {
      message.error(t('projects.config.test_command_copy_failed'));
    }
  };

  return (
    <div data-testid="project-config">
      <Card
        className="glass-panel"
        bordered={false}
        style={{ marginBottom: 20 }}
        data-testid="project-config-test-commands"
      >
        <Space direction="vertical" size={16} style={{ width: '100%' }}>
          <div>
            <Text strong>{t('projects.config.test_commands')}</Text>
            <div>
              <Text type="secondary" style={{ fontSize: 12 }}>
                {t('projects.config.test_commands_hint')}
              </Text>
            </div>
          </div>

          {commandsReady ? (
            <>
              <div style={{ display: 'grid', gap: 6 }}>
                <Text strong style={{ fontSize: 13 }}>{t('projects.config.expanded_command')}</Text>
                <div style={{ position: 'relative' }}>
                  <pre style={commandStyle}>{commandLines.expanded}</pre>
                  <Button
                    type="text"
                    size="small"
                    icon={<CopyOutlined />}
                    onClick={() => handleCopy(commandLines.expanded)}
                    data-testid="project-expanded-command-copy"
                    style={{ position: 'absolute', top: '50%', right: 4, transform: 'translateY(-50%)' }}
                  />
                </div>
              </div>

              <div style={{ display: 'grid', gap: 6 }}>
                <div style={{ display: 'grid', gap: 4 }}>
                  <Text strong style={{ fontSize: 13 }}>{t('projects.config.config_file_command')}</Text>
                  <Text type="secondary" style={{ fontSize: 12 }}>
                    {t('projects.config.config_file_command_hint', {
                      filePath: commandLines.configFilePath,
                      workingDirectory: commandLines.workingDirectory,
                    })}
                  </Text>
                </div>
                <div style={{ position: 'relative' }}>
                  <pre style={commandStyle}>{commandLines.configFile}</pre>
                  <Button
                    type="text"
                    size="small"
                    icon={<CopyOutlined />}
                    onClick={() => handleCopy(commandLines.configFile)}
                    data-testid="project-config-file-command-copy"
                    style={{ position: 'absolute', top: '50%', right: 4, transform: 'translateY(-50%)' }}
                  />
                </div>
              </div>
            </>
          ) : (
            <Text type="secondary" data-testid="project-config-test-commands-placeholder">
              {t('projects.config.test_commands_loading')}
            </Text>
          )}
        </Space>
      </Card>

      <SchemaForm schema={schema} value={value} onChange={setValue} />
      <Button
        type="primary"
        onClick={handleSave}
        loading={saving}
        style={{ marginTop: 16 }}
        data-testid="save-config-btn"
      >
        {t('projects.config.save')}
      </Button>
    </div>
  );
};
