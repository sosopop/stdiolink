import React from 'react';
import { useTranslation } from 'react-i18next';
import { Form } from 'antd';
import type { FieldMeta } from '@/types/service';
import { FieldRenderer } from '@/components/SchemaForm/FieldRenderer';
import { Typography } from 'antd';
import styles from './ParamForm.module.css';

interface ParamFormProps {
  params: FieldMeta[];
  values: Record<string, unknown>;
  onChange: (values: Record<string, unknown>) => void;
}

export const ParamForm: React.FC<ParamFormProps> = ({ params, values, onChange }) => {
  const { t } = useTranslation();

  if (!params || params.length === 0) {
    return (
      <Typography.Text type="secondary" data-testid="no-params" style={{ fontSize: 13 }}>
        {t('driverlab.command.no_params')}
      </Typography.Text>
    );
  }

  const handleFieldChange = (name: string, value: unknown) => {
    onChange({ ...values, [name]: value });
  };

  return (
    <Form layout="vertical" data-testid="param-form" className={styles.driverParamForm}>
      {params.map((field) => (
        <FieldRenderer
          key={field.name}
          field={field}
          value={values[field.name]}
          onChange={(v) => handleFieldChange(field.name, v)}
        />
      ))}
    </Form>
  );
};
