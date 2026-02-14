import React from 'react';
import { Select, Button, Space, Typography } from 'antd';
import { PlayCircleOutlined, StopOutlined } from '@ant-design/icons';
import type { CommandMeta } from '@/types/driver';
import { ParamForm } from './ParamForm';
import { CommandLineExample } from './CommandLineExample';

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
  if (!commands || commands.length === 0) {
    return (
      <div data-testid="command-panel">
        <Typography.Text type="secondary" data-testid="waiting-meta">
          Waiting for driver metadata...
        </Typography.Text>
      </div>
    );
  }

  const currentCmd = commands.find((c) => c.name === selectedCommand);

  return (
    <div data-testid="command-panel">
      <div style={{ marginBottom: 12 }}>
        <Typography.Text strong style={{ display: 'block', marginBottom: 4 }}>
          Command
        </Typography.Text>
        <Select
          value={selectedCommand}
          onChange={onSelectCommand}
          placeholder="Select command"
          style={{ width: '100%' }}
          data-testid="command-select"
          options={commands.map((c) => ({
            label: c.name,
            value: c.name,
          }))}
        />
      </div>

      {currentCmd && (
        <div style={{ marginBottom: 12 }}>
          {currentCmd.description && (
            <Typography.Paragraph type="secondary" style={{ marginBottom: 8 }}>
              {currentCmd.description}
            </Typography.Paragraph>
          )}
          <ParamForm
            params={currentCmd.params}
            values={commandParams}
            onChange={onParamsChange}
          />
        </div>
      )}

      <Space style={{ marginBottom: 12 }}>
        <Button
          type="primary"
          icon={<PlayCircleOutlined />}
          onClick={onExec}
          disabled={!connected || !selectedCommand || executing}
          data-testid="exec-btn"
        >
          Execute
        </Button>
        <Button
          icon={<StopOutlined />}
          onClick={onCancel}
          disabled={!executing}
          data-testid="cancel-btn"
        >
          Cancel
        </Button>
      </Space>

      <CommandLineExample
        driverId={driverId}
        command={selectedCommand}
        params={commandParams}
      />
    </div>
  );
};
