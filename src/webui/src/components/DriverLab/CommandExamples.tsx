import React from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Space, Tag, Typography } from 'antd';
import type { CommandExampleMeta } from '@/types/driver';

interface CommandExamplesProps {
  examples: CommandExampleMeta[];
  onApply: (params: Record<string, unknown>) => void;
}

function previewParams(params: Record<string, unknown>): string {
  try {
    const text = JSON.stringify(params);
    if (text.length > 100) {
      return `${text.slice(0, 97)}...`;
    }
    return text;
  } catch {
    return '{}';
  }
}

export const CommandExamples: React.FC<CommandExamplesProps> = ({ examples, onApply }) => {
  const { t } = useTranslation();
  if (!examples || examples.length === 0) {
    return null;
  }

  return (
    <div data-testid="command-examples" style={{ marginBottom: 12 }}>
      <Typography.Text
        type="secondary"
        style={{
          display: 'block',
          marginBottom: 8,
          fontSize: 11,
          fontWeight: 600,
          textTransform: 'uppercase',
          letterSpacing: '1px',
        }}
      >
        {t('driverlab.command.examples_title')}
      </Typography.Text>
      <Space direction="vertical" style={{ width: '100%' }} size={8}>
        {examples.map((ex, index) => (
          <div
            key={`${ex.mode}-${index}`}
            data-testid={`example-item-${index}`}
            style={{
              border: '1px solid var(--surface-border)',
              borderRadius: 8,
              padding: 10,
              background: 'rgba(255,255,255,0.02)',
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8 }}>
              <Typography.Text strong>{ex.description}</Typography.Text>
              <Tag>{ex.mode}</Tag>
            </div>
            <Typography.Paragraph
              type="secondary"
              style={{ margin: '6px 0', fontSize: 12, fontFamily: 'monospace' }}
            >
              {previewParams(ex.params)}
            </Typography.Paragraph>
            <Button
              size="small"
              onClick={() => onApply(ex.params)}
              data-testid={`apply-example-${index}`}
            >
              {t('driverlab.command.apply_example')}
            </Button>
          </div>
        ))}
      </Space>
    </div>
  );
};

