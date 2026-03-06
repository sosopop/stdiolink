import React from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Tag, Typography } from 'antd';
import type { CommandExampleMeta } from '@/types/driver';

interface CommandExamplesProps {
  examples: CommandExampleMeta[];
  onApply: (params: Record<string, unknown>) => void;
}

export const CommandExamples: React.FC<CommandExamplesProps> = ({ examples, onApply }) => {
  const { t } = useTranslation();
  if (!examples || examples.length === 0) {
    return null;
  }

  const handleParamsWheel: React.WheelEventHandler<HTMLElement> = (event) => {
    const target = event.currentTarget;
    if (event.deltaY !== 0) {
      target.scrollLeft += event.deltaY;
      event.preventDefault();
    }
  };

  return (
    <div data-testid="command-examples" style={{ marginBottom: 12, display: 'grid', gap: 8 }}>
      {examples.map((ex, index) => {
        const paramsLine = JSON.stringify(ex.params);
        return (
          <div
            key={`${ex.mode}-${index}`}
            data-testid={`example-item-${index}`}
            style={{
              border: '1px solid var(--surface-border)',
              borderRadius: 8,
              padding: 8,
              background: 'rgba(255,255,255,0.02)',
            }}
          >
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 8 }}>
              <Typography.Text data-testid={`example-description-${index}`} strong style={{ margin: 0 }}>
                {ex.description}
              </Typography.Text>
              <Tag data-testid={`example-mode-${index}`} color={ex.mode === 'console' ? 'blue' : 'default'}>
                {ex.mode}
              </Tag>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Typography.Text
                data-testid={`example-params-${index}`}
                title={paramsLine}
                className="hide-scrollbar"
                onWheel={handleParamsWheel}
                style={{
                  flex: 1,
                  margin: 0,
                  padding: '6px 10px',
                  fontSize: 12,
                  borderRadius: 6,
                  border: '1px dashed var(--surface-border)',
                  background: 'rgba(255,255,255,0.03)',
                  fontFamily: 'Consolas, Monaco, "Courier New", monospace',
                  whiteSpace: 'nowrap',
                  overflowX: 'auto',
                  overflowY: 'hidden',
                }}
              >
                {paramsLine}
              </Typography.Text>
              <Button
                size="small"
                onClick={() => onApply(ex.params)}
                data-testid={`apply-example-${index}`}
              >
                {t('driverlab.command.apply_example')}
              </Button>
            </div>
          </div>
        );
      })}
    </div>
  );
};
