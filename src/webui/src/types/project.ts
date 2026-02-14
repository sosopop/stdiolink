import type { Instance } from './instance';

export interface Project {
  id: string;
  name: string;
  serviceId: string;
  enabled: boolean;
  valid: boolean;
  error?: string;
  config: Record<string, unknown>;
  schedule: Schedule;
  instanceCount: number;
  status: string;
}

export type ScheduleType = 'manual' | 'fixed_rate' | 'daemon';

export interface Schedule {
  type: ScheduleType;
  intervalMs?: number;
  maxConcurrent?: number;
  restartDelayMs?: number;
  maxConsecutiveFailures?: number;
}

export interface ProjectRuntime {
  id: string;
  enabled: boolean;
  valid: boolean;
  error?: string;
  status: 'running' | 'stopped' | 'disabled' | 'invalid';
  runningInstances: number;
  instances: Instance[];
  schedule: {
    type: string;
    timerActive: boolean;
    restartSuppressed: boolean;
    consecutiveFailures: number;
    shuttingDown: boolean;
    autoRestarting: boolean;
  };
}

export interface CreateProjectRequest {
  id: string;
  name: string;
  serviceId: string;
  enabled?: boolean;
  config?: Record<string, unknown>;
  schedule?: Schedule;
}

export interface UpdateProjectRequest {
  name?: string;
  serviceId?: string;
  enabled?: boolean;
  config?: Record<string, unknown>;
  schedule?: Schedule;
}
