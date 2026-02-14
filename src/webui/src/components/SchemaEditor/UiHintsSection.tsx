import React from 'react';
import { Input, InputNumber, Switch, Typography } from 'antd';

const { Text } = Typography;

interface UiHints {
  group?: string;
  order?: number;
  advanced?: boolean;
  readonly?: boolean;
  placeholder?: string;
  unit?: string;
}

interface UiHintsSectionProps {
  hints: UiHints;
  onChange: (hints: UiHints) => void;
}

export const UiHintsSection: React.FC<UiHintsSectionProps> = ({ hints, onChange }) => {
  const update = (key: keyof UiHints, value: unknown) => {
    onChange({ ...hints, [key]: value });
  };

  return (
    <div data-testid="ui-hints-section">
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Group</Text>
        <Input
          value={hints.group ?? ''}
          onChange={(e) => update('group', e.target.value || undefined)}
          placeholder="Field group name"
          data-testid="hint-group"
        />
      </div>
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Order</Text>
        <InputNumber
          value={hints.order}
          onChange={(v) => update('order', v)}
          data-testid="hint-order"
          style={{ width: '100%' }}
        />
      </div>
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Advanced</Text>
        <Switch
          checked={hints.advanced ?? false}
          onChange={(v) => update('advanced', v)}
          data-testid="hint-advanced"
        />
      </div>
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Read Only</Text>
        <Switch
          checked={hints.readonly ?? false}
          onChange={(v) => update('readonly', v)}
          data-testid="hint-readonly"
        />
      </div>
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Placeholder</Text>
        <Input
          value={hints.placeholder ?? ''}
          onChange={(e) => update('placeholder', e.target.value || undefined)}
          data-testid="hint-placeholder"
        />
      </div>
      <div style={{ marginBottom: 8 }}>
        <Text type="secondary" style={{ display: 'block', marginBottom: 4 }}>Unit</Text>
        <Input
          value={hints.unit ?? ''}
          onChange={(e) => update('unit', e.target.value || undefined)}
          data-testid="hint-unit"
        />
      </div>
    </div>
  );
};
