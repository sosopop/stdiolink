export interface ServerStatus {
  status: string;
  version: string;
  uptimeMs: number;
  startedAt: string;
  host: string;
  port: number;
  dataRoot: string;
  serviceProgram: string;
  counts: {
    services: number;
    projects: {
      total: number;
      valid: number;
      invalid: number;
      enabled: number;
      disabled: number;
    };
    instances: {
      total: number;
      running: number;
    };
    drivers: number;
  };
  system: {
    platform: string;
    cpuCores: number;
    totalMemoryBytes?: number;
  };
}

export interface ServerEvent {
  type: string;
  data: Record<string, unknown>;
}
