import React from 'react';
import { useTranslation } from 'react-i18next';
import { Select, Button, Space, Typography } from 'antd';
import { PlayCircleOutlined, ReloadOutlined, StopOutlined } from '@ant-design/icons';
import type { CommandMeta } from '@/types/driver';
import type { FieldMeta } from '@/types/service';
import { ParamForm } from './ParamForm';
import { CommandLineExample } from './CommandLineExample';
import { selectDriverLabExamples } from './exampleMeta';
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
  onReset: () => void;
}

function isMissingRequiredValue(field: FieldMeta, value: unknown): boolean {
  if (value === undefined || value === null) {
    return true;
  }

  if ((field.type === 'string' || field.type === 'enum' || field.type === 'any') && typeof value === 'string') {
    return value.trim() === '';
  }

  if (field.type === 'array') {
    return !Array.isArray(value) || value.length === 0;
  }

  return false;
}

function validateFields(
  fields: FieldMeta[] | undefined,
  values: Record<string, unknown>,
  message: string,
  basePath = '',
): Record<string, string> {
  if (!fields || fields.length === 0) {
    return {};
  }

  const errors: Record<string, string> = {};

  for (const field of fields) {
    const path = basePath ? `${basePath}.${field.name}` : field.name;
    const value = values[field.name];
    const childRequired = new Set(field.requiredKeys ?? []);

    if (field.required && isMissingRequiredValue(field, value)) {
      errors[path] = message;
      continue;
    }

    if (field.type === 'object' && value && typeof value === 'object' && !Array.isArray(value)) {
      const objectValue = value as Record<string, unknown>;
      const nestedFields = (field.fields ?? []).map((child) => (
        childRequired.has(child.name) ? { ...child, required: true } : child
      ));
      Object.assign(errors, validateFields(nestedFields, objectValue, message, path));
      continue;
    }

    if (field.type === 'array' && Array.isArray(value) && field.items) {
      value.forEach((item, index) => {
        const itemPath = `${path}[${index}]`;
        if (field.items?.required && isMissingRequiredValue(field.items, item)) {
          errors[itemPath] = message;
          return;
        }

        if (field.items?.type === 'object' && item && typeof item === 'object' && !Array.isArray(item)) {
          const objectItem = item as Record<string, unknown>;
          const nestedFields = (field.items.fields ?? []).map((child) => (
            field.items?.requiredKeys?.includes(child.name) ? { ...child, required: true } : child
          ));
          Object.assign(errors, validateFields(nestedFields, objectItem, message, itemPath));
        }
      });
    }
  }

  return errors;
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
  onReset,
}) => {
  const { t } = useTranslation();
  const [fieldErrors, setFieldErrors] = React.useState<Record<string, string>>({});
  const currentCmd = commands.find((c) => c.name === selectedCommand);
  const examples = selectDriverLabExamples(currentCmd?.examples);
  const requiredMessage = t('schema.required');

  React.useEffect(() => {
    setFieldErrors({});
  }, [selectedCommand]);

  if (!commands || commands.length === 0) {
    return (
      <div data-testid="command-panel">
        <Typography.Text type="secondary" data-testid="waiting-meta">
          {t('driverlab.command.waiting_meta')}
        </Typography.Text>
      </div>
    );
  }

  const handleParamsChange = (params: Record<string, unknown>) => {
    onParamsChange(params);
    if (currentCmd && Object.keys(fieldErrors).length > 0) {
      setFieldErrors(validateFields(currentCmd.params, params, requiredMessage));
    }
  };

  const handleExec = () => {
    const nextErrors = validateFields(currentCmd?.params, commandParams, requiredMessage);
    setFieldErrors(nextErrors);
    if (Object.keys(nextErrors).length > 0) {
      return;
    }
    onExec();
  };

  const handleSelectCommand = (name: string) => {
    setFieldErrors({});
    onSelectCommand(name);
  };

  const handleReset = () => {
    setFieldErrors({});
    onReset();
  };

  return (
    <div
      data-testid="command-panel"
      style={{ display: 'flex', flexDirection: 'column', height: '100%', minWidth: 0, overflow: 'hidden' }}
    >
      {/* 顶部固定：命令选择 */}
      <div style={{ marginBottom: 16, flexShrink: 0 }}>
        <Typography.Text type="secondary" style={{ display: 'block', marginBottom: 8, fontSize: 11, fontWeight: 600, textTransform: 'uppercase', letterSpacing: '1px' }}>
          {t('driverlab.command.select_command')}
        </Typography.Text>
        <Select
          value={selectedCommand}
          onChange={handleSelectCommand}
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
        <div style={{ flex: 1, overflowY: 'auto', marginBottom: 16, paddingRight: 8, minHeight: 0, minWidth: 0 }}>
          {currentCmd.description && (
            <div style={{ padding: '8px 12px', background: 'rgba(255,255,255,0.03)', borderRadius: 8, marginBottom: 16, borderLeft: '3px solid var(--brand-primary)' }}>
              <Typography.Paragraph type="secondary" style={{ margin: 0, fontSize: 13, lineHeight: 1.5 }}>
                {currentCmd.description}
              </Typography.Paragraph>
            </div>
          )}
          <CommandExamples
            examples={examples}
            onApply={(params) => handleParamsChange(params)}
          />
          <ParamForm
            params={currentCmd.params}
            values={commandParams}
            errors={fieldErrors}
            onChange={handleParamsChange}
          />
        </div>
      )}

      {/* 底部固定：操作按钮与示例 */}
      <div style={{ marginTop: 'auto', paddingTop: 16, borderTop: '1px solid var(--surface-border)', flexShrink: 0 }}>
        <Space style={{ marginBottom: 16, width: '100%' }}>
          <Button
            type="primary"
            icon={<PlayCircleOutlined />}
            onClick={handleExec}
            disabled={!connected || !selectedCommand || executing}
            data-testid="exec-btn"
            style={{ borderRadius: 8, height: 36, padding: '0 20px' }}
          >
            {t('driverlab.command.execute')}
          </Button>
          <Button
            icon={<ReloadOutlined />}
            onClick={handleReset}
            disabled={!currentCmd}
            data-testid="reset-btn"
            style={{ borderRadius: 8, height: 36 }}
          >
            {t('common.reset')}
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
