import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Modal, Steps, Input, Switch, Form, Card, Typography, Alert } from 'antd';
import { SchemaForm } from '@/components/SchemaForm/SchemaForm';
import { ScheduleForm } from './ScheduleForm';
import type { ButtonProps } from 'antd';
import type { ServiceInfo, ServiceDetail } from '@/types/service';
import type { Schedule, CreateProjectRequest } from '@/types/project';

const { Text } = Typography;
const ID_PATTERN = /^[a-zA-Z0-9_-]+$/;

interface ProjectCreateWizardProps {
  open: boolean;
  onClose: () => void;
  onCreate: (data: CreateProjectRequest) => Promise<boolean>;
  services: ServiceInfo[];
  getServiceDetail: (id: string) => Promise<ServiceDetail | null>;
}

export const ProjectCreateWizard: React.FC<ProjectCreateWizardProps> = ({
  open, onClose, onCreate, services, getServiceDetail,
}) => {
  const { t } = useTranslation();
  const [step, setStep] = useState(0);
  const [selectedService, setSelectedService] = useState<string | null>(null);
  const [serviceDetail, setServiceDetail] = useState<ServiceDetail | null>(null);
  const [projectId, setProjectId] = useState('');
  const [projectName, setProjectName] = useState('');
  const [enabled, setEnabled] = useState(true);
  const [config, setConfig] = useState<Record<string, unknown>>({});
  const [schedule, setSchedule] = useState<Schedule>({ type: 'manual' });
  const [error, setError] = useState<string | null>(null);
  const [creating, setCreating] = useState(false);

  const idValid = projectId.length > 0 && ID_PATTERN.test(projectId);
  const idError = projectId.length > 0 && !ID_PATTERN.test(projectId) ? 'Only letters, numbers, _ and - allowed' : null;
  const nameValid = projectName.length > 0;

  const reset = () => {
    setStep(0);
    setSelectedService(null);
    setServiceDetail(null);
    setProjectId('');
    setProjectName('');
    setEnabled(true);
    setConfig({});
    setSchedule({ type: 'manual' });
    setError(null);
    setCreating(false);
  };

  const handleSelectService = async (id: string) => {
    setSelectedService(id);
    const detail = await getServiceDetail(id);
    setServiceDetail(detail);
  };

  const handleOk = async () => {
    if (step === 0) {
      if (!selectedService) return;
      setStep(1);
    } else if (step === 1) {
      if (!idValid || !nameValid) return;
      setStep(2);
    } else if (step === 2) {
      setStep(3);
    } else {
      setCreating(true);
      setError(null);
      const ok = await onCreate({
        id: projectId,
        name: projectName,
        serviceId: selectedService!,
        enabled,
        config,
        schedule,
      });
      if (ok) {
        reset();
        onClose();
      } else {
        setError(t('projects.create_wizard.fail'));
        setCreating(false);
      }
    }
  };

  const handleCancel = () => {
    reset();
    onClose();
  };

  const okDisabled =
    (step === 0 && !selectedService) ||
    (step === 1 && (!idValid || !nameValid));
  const okButtonProps: ButtonProps & { 'data-testid': string } = {
    disabled: okDisabled,
    loading: creating,
    'data-testid': 'wizard-ok',
  };
  const cancelButtonProps: ButtonProps & { 'data-testid': string } = {
    'data-testid': 'wizard-cancel',
  };

  return (
    <Modal
      title="Create Project"
      open={open}
      onOk={handleOk}
      onCancel={handleCancel}
      okText={step < 3 ? t('projects.create_wizard.next') : t('projects.create_wizard.create')}
      okButtonProps={okButtonProps}
      cancelButtonProps={cancelButtonProps}
      width={720}
      data-testid="create-wizard"
    >
      <Steps current={step} size="small" style={{ marginBottom: 24 }} items={[
        { title: t('projects.create_wizard.step_service') },
        { title: t('projects.create_wizard.step_info') },
        { title: t('projects.create_wizard.step_config') },
        { title: t('projects.create_wizard.step_schedule') },
      ]} />

      {step === 0 && (
        <div data-testid="step-service">
          {services.map((svc) => (
            <Card
              key={svc.id}
              size="small"
              hoverable
              onClick={() => handleSelectService(svc.id)}
              style={{
                marginBottom: 8,
                borderColor: selectedService === svc.id ? '#6366F1' : undefined,
              }}
              data-testid={`service-card-${svc.id}`}
            >
              <Text strong>{svc.name}</Text>
              <br />
              <Text type="secondary">{svc.id} · v{svc.version}</Text>
            </Card>
          ))}
        </div>
      )}

      {step === 1 && (
        <div data-testid="step-info">
          <Form layout="vertical">
            <Form.Item label={t('projects.create_wizard.project_id')} required validateStatus={idError ? 'error' : undefined} help={idError}>
              <Input
                value={projectId}
                onChange={(e) => setProjectId(e.target.value)}
                placeholder="my_project"
                data-testid="project-id-input"
              />
            </Form.Item>
            <Form.Item label={t('projects.create_wizard.name')} required>
              <Input
                value={projectName}
                onChange={(e) => setProjectName(e.target.value)}
                placeholder="My Project"
                data-testid="project-name-input"
              />
            </Form.Item>
            <Form.Item label={t('projects.create_wizard.enabled')}>
              <Switch checked={enabled} onChange={setEnabled} data-testid="project-enabled" />
            </Form.Item>
          </Form>
        </div>
      )}

      {step === 2 && (
        <div data-testid="step-config">
          {serviceDetail?.configSchemaFields && serviceDetail.configSchemaFields.length > 0 ? (
            <SchemaForm
              schema={serviceDetail.configSchemaFields}
              value={config}
              onChange={setConfig}
            />
          ) : (
            <Text type="secondary">No configuration schema defined for this service.</Text>
          )}
        </div>
      )}

      {step === 3 && (
        <div data-testid="step-schedule">
          <ScheduleForm value={schedule} onChange={setSchedule} />
        </div>
      )}

      {error && <Alert type="error" message={error} data-testid="wizard-error" style={{ marginTop: 16 }} />}
    </Modal>
  );
};
