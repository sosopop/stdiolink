import React from 'react';
import { useTranslation } from 'react-i18next';
import { Button, Tooltip, Typography } from 'antd';
import { AlignLeftOutlined } from '@ant-design/icons';
import type { CommandExampleMeta } from '@/types/driver';

interface CommandExamplesProps {
  examples: CommandExampleMeta[];
  onApply: (params: Record<string, unknown>) => void;
}

export const CommandExamples: React.FC<CommandExamplesProps> = ({ examples, onApply }) => {
  const { t } = useTranslation();
  const [wrappedExamples, setWrappedExamples] = React.useState<Record<string, boolean>>({});
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
    <div
      data-testid="command-examples"
      style={{ marginBottom: 12, display: 'grid', gap: 8, minWidth: 0, maxWidth: '100%' }}
    >
      {examples.map((ex, index) => {
        const exampleKey = `${ex.mode}-${index}`;
        const paramsLine = JSON.stringify(ex.params);
        const wrapped = wrappedExamples[exampleKey] === true;
        return (
          <div
            key={exampleKey}
            data-testid={`example-item-${index}`}
            style={{
              border: '1px solid var(--surface-border)',
              borderRadius: 8,
              padding: 8,
              background: 'rgba(255,255,255,0.02)',
              minWidth: 0,
              maxWidth: '100%',
              overflow: 'hidden',
            }}
          >
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 8 }}>
              <Typography.Text data-testid={`example-description-${index}`} strong style={{ margin: 0 }}>
                {t('driverlab.command.examples_title')}
              </Typography.Text>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, minWidth: 0, maxWidth: '100%' }}>
              <div
                data-testid={`example-scroll-${index}`}
                className="hide-scrollbar"
                onWheel={wrapped ? undefined : handleParamsWheel}
                style={{
                  flex: '1 1 0',
                  width: 0,
                  minWidth: 0,
                  maxWidth: '100%',
                  overflowX: wrapped ? 'hidden' : 'auto',
                  overflowY: 'hidden',
                }}
              >
                <Typography.Text
                  data-testid={`example-params-${index}`}
                  title={paramsLine}
                  style={{
                    display: 'block',
                    width: '100%',
                    minWidth: wrapped ? 0 : 'max-content',
                    boxSizing: 'border-box',
                    margin: 0,
                    padding: '6px 10px',
                    fontSize: 12,
                    borderRadius: 6,
                    border: '1px dashed var(--surface-border)',
                    background: 'rgba(255,255,255,0.03)',
                    fontFamily: 'Consolas, Monaco, "Courier New", monospace',
                    whiteSpace: wrapped ? 'pre-wrap' : 'nowrap',
                    wordBreak: wrapped ? 'break-word' : 'normal',
                    overflowWrap: wrapped ? 'anywhere' : 'normal',
                  }}
                >
                  {paramsLine}
                </Typography.Text>
              </div>
              <Tooltip title={wrapped ? t('driverlab.command.wrap_disable') : t('driverlab.command.wrap_enable')}>
                <Button
                  size="small"
                  type={wrapped ? 'default' : 'text'}
                  icon={<AlignLeftOutlined />}
                  aria-label={wrapped ? t('driverlab.command.wrap_disable') : t('driverlab.command.wrap_enable')}
                  onClick={() =>
                    setWrappedExamples((current) => ({
                      ...current,
                      [exampleKey]: !wrapped,
                    }))
                  }
                  style={{ flexShrink: 0 }}
                  data-testid={`toggle-wrap-${index}`}
                />
              </Tooltip>
              <Button
                size="small"
                onClick={() => onApply(ex.params)}
                style={{ flexShrink: 0 }}
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
