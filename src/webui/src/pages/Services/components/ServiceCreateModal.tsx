import React, { useState } from 'react';
import { Modal, Steps, Radio, Input, Space, Typography, Alert } from 'antd';

const { Text } = Typography;

const ID_PATTERN = /^[A-Za-z0-9_-]+$/;

const TEMPLATES = [
  { value: 'empty', label: 'Empty', description: 'Blank service with minimal files' },
  { value: 'basic', label: 'Basic', description: 'Basic service with sample config' },
  { value: 'driver_demo', label: 'Driver Demo', description: 'Demo service with driver integration' },
];

interface ServiceCreateModalProps {
  open: boolean;
  onClose: () => void;
  onCreate: (data: { id: string; template: string }) => Promise<boolean>;
}

export const ServiceCreateModal: React.FC<ServiceCreateModalProps> = ({ open, onClose, onCreate }) => {
  const [step, setStep] = useState(0);
  const [template, setTemplate] = useState('empty');
  const [serviceId, setServiceId] = useState('');
  const [error, setError] = useState<string | null>(null);
  const [creating, setCreating] = useState(false);

  const idValid = serviceId.length > 0 && ID_PATTERN.test(serviceId);
  const idError = serviceId.length > 0 && !ID_PATTERN.test(serviceId) ? 'Only letters, numbers, _ and - allowed' : null;

  const reset = () => {
    setStep(0);
    setTemplate('empty');
    setServiceId('');
    setError(null);
    setCreating(false);
  };

  const handleOk = async () => {
    if (step === 0) {
      setStep(1);
    } else if (step === 1) {
      if (!idValid) return;
      setStep(2);
    } else {
      setCreating(true);
      setError(null);
      const ok = await onCreate({ id: serviceId, template });
      if (ok) {
        reset();
        onClose();
      } else {
        setError('Failed to create service');
        setCreating(false);
      }
    }
  };

  const handleCancel = () => {
    reset();
    onClose();
  };

  const okText = step < 2 ? 'Next' : 'Create';
  const okDisabled = step === 1 && !idValid;

  return (
    <Modal
      title="Create Service"
      open={open}
      onOk={handleOk}
      onCancel={handleCancel}
      okText={okText}
      okButtonProps={{ disabled: okDisabled, loading: creating, 'data-testid': 'modal-ok' } as any}
      cancelButtonProps={{ 'data-testid': 'modal-cancel' } as any}
      data-testid="create-modal"
    >
      <Steps current={step} size="small" style={{ marginBottom: 24 }} items={[
        { title: 'Template' },
        { title: 'Info' },
        { title: 'Confirm' },
      ]} />

      {step === 0 && (
        <Radio.Group value={template} onChange={(e) => setTemplate(e.target.value)} data-testid="template-select">
          <Space direction="vertical">
            {TEMPLATES.map((t) => (
              <Radio key={t.value} value={t.value} data-testid={`template-${t.value}`}>
                <Text strong>{t.label}</Text>
                <br />
                <Text type="secondary">{t.description}</Text>
              </Radio>
            ))}
          </Space>
        </Radio.Group>
      )}

      {step === 1 && (
        <div>
          <Text>Service ID</Text>
          <Input
            value={serviceId}
            onChange={(e) => setServiceId(e.target.value)}
            placeholder="my_service"
            status={idError ? 'error' : undefined}
            data-testid="service-id-input"
            style={{ marginTop: 8 }}
          />
          {idError && <Text type="danger" data-testid="id-error">{idError}</Text>}
        </div>
      )}

      {step === 2 && (
        <div data-testid="confirm-step">
          <Text>Template: <Text strong>{template}</Text></Text>
          <br />
          <Text>Service ID: <Text strong>{serviceId}</Text></Text>
        </div>
      )}

      {error && <Alert type="error" message={error} data-testid="create-error" style={{ marginTop: 16 }} />}
    </Modal>
  );
};
