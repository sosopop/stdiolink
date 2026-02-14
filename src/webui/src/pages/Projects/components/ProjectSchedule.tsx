import React, { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Button, message } from 'antd';
import { ScheduleForm } from './ScheduleForm';
import type { Schedule } from '@/types/project';

interface ProjectScheduleProps {
  schedule: Schedule;
  onSave: (schedule: Schedule) => Promise<boolean>;
}

export const ProjectSchedule: React.FC<ProjectScheduleProps> = ({ schedule, onSave }) => {
  const { t } = useTranslation();
  const [value, setValue] = useState<Schedule>(schedule);
  const [saving, setSaving] = useState(false);

  const handleSave = async () => {
    setSaving(true);
    const ok = await onSave(value);
    if (ok) message.success(t('projects.schedule.save_success'));
    else message.error(t('projects.schedule.save_fail'));
    setSaving(false);
  };

  return (
    <div data-testid="project-schedule">
      <ScheduleForm value={value} onChange={setValue} />
      <Button
        type="primary"
        onClick={handleSave}
        loading={saving}
        style={{ marginTop: 16 }}
        data-testid="save-schedule-btn"
      >
        {t('projects.schedule.save')}
      </Button>
    </div>
  );
};
