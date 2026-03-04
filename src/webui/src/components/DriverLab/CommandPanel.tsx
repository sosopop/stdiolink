import React from 'react';
import { useTranslation } from 'react-i18next';
import { Select, Button, Space, Typography } from 'antd';
import { PlayCircleOutlined, StopOutlined } from '@ant-design/icons';
import type { CommandMeta } from '@/types/driver';
import { ParamForm } from './ParamForm';
import { CommandLineExample } from './CommandLineExample';
import { normalizeCommandExamples } from './exampleMeta';
import { CommandExamples } from './CommandExamples';

interface CommandPanelProps {
  commands: CommandMeta[];
  selectedCommand: string | null;
  commandParams: Record<string, unknown>;
  executing: boolean;
  connected: boolean;
  driverId: string | null;
  onSelectCommand: (name: string) => void;
  onParamsChange: (params: Record<string, unknown>) => void;
  onExec: () => void;
  onCancel: () => void;
}

export const CommandPanel: React.FC<CommandPanelProps> = ({
  commands,
  selectedCommand,
  commandParams,
  executing,
  connected,
  driverId,
  onSelectCommand,
  onParamsChange,
  onExec,
  onCancel,
}) => {
  const { t } = useTranslation();

  if (!commands || commands.length === 0) {
    return (
      <div data-testid="command-panel">
        <Typography.Text type="secondary" data-testid="waiting-meta">
          {t('driverlab.command.waiting_meta')}
        </Typography.Text>
      </div>
    );
  }

  const currentCmd = commands.find((c) => c.name === selectedCommand);
  const examples = normalizeCommandExamples(currentCmd?.examples);

  return (
    <div data-testid="command-panel" style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
      {/* 顶部固定：命令选择 */}
      <div style={{ marginBottom: 16, flexShrink: 0 }}>
        <Typography.Text type="secondary" style={{ display: 'block', marginBottom: 8, fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '1px' }}>
          {t('driverlab.command.select_command')}
        </Typography.Text>
        <Select
          value={selectedCommand}
          onChange={onSelectCommand}
          placeholder={t('driverlab.command.select_placeholder')}
          style={{ width: '100%' }}
          data-testid="command-select"
          options={commands.map((c) => ({
            label: c.name,
            value: c.name,
          }))}
        />
      </div>

      {/* 中部滚动：参数表单 */}
      {currentCmd && (
        <div style={{ flex: 1, overflowY: 'auto', marginBottom: 16, paddingRight: 8, minHeight: 0 }}>
          {currentCmd.description && (
            <div style={{ padding: '8px 12px', background: 'rgba(255,255,255,0.03)', borderRadius: 8, marginBottom: 16, borderLeft: '3px solid var(--brand-primary)' }}>
              <Typography.Paragraph type="secondary" style={{ margin: 0, fontSize: 13, lineHeight: 1.5 }}>
                {currentCmd.description}
              </Typography.Paragraph>
            </div>
          )}
          <CommandExamples
            examples={examples}
            onApply={(params) => onParamsChange(params)}
          />
          <ParamForm
            params={currentCmd.params}
            values={commandParams}
            onChange={onParamsChange}
          />
        </div>
      )}

      {/* 底部固定：操作按钮与示例 */}
      <div style={{ marginTop: 'auto', paddingTop: 16, borderTop: '1px solid var(--surface-border)', flexShrink: 0 }}>
        <Space style={{ marginBottom: 16, width: '100%' }}>
          <Button
            type="primary"
            icon={<PlayCircleOutlined />}
            onClick={onExec}
            disabled={!connected || !selectedCommand || executing}
            data-testid="exec-btn"
            style={{ borderRadius: 8, height: 36, padding: '0 20px' }}
          >
            {t('driverlab.command.execute')}
          </Button>
          <Button
            icon={<StopOutlined />}
            onClick={onCancel}
            disabled={!executing}
            data-testid="cancel-btn"
            style={{ borderRadius: 8, height: 36 }}
          >
            {t('common.cancel')}
          </Button>
        </Space>

        <CommandLineExample
          driverId={driverId}
          command={selectedCommand}
          params={commandParams}
        />
      </div>
    </div>
  );
};
