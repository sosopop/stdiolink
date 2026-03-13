import type { TFunction } from 'i18next';

export function translateProjectStatus(t: TFunction, status?: string | null): string {
  const normalizedStatus = status || 'stopped';
  return t(`common.status_${normalizedStatus}`, normalizedStatus);
}

export function translateScheduleType(t: TFunction, type?: string | null): string {
  const normalizedType = type || 'manual';
  return t(`projects.schedule.${normalizedType}`, normalizedType);
}
