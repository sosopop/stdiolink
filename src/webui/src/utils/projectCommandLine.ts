import { renderCliArgs } from './cliArgs';

function quote(value: string): string {
  return JSON.stringify(value);
}

export interface ProjectCommandLinesOptions {
  serviceDir: string | null;
  dataRoot: string | null;
  config: Record<string, unknown>;
  configFileName?: string;
}

export interface ProjectCommandLines {
  expanded: string;
  configFile: string;
}

export function buildProjectCommandLines({
  serviceDir,
  dataRoot,
  config,
  configFileName = '<project-config.json>',
}: ProjectCommandLinesOptions): ProjectCommandLines {
  if (!serviceDir || !dataRoot) {
    return { expanded: '', configFile: '' };
  }

  const baseArgs = [quote(serviceDir), `--data-root=${quote(dataRoot)}`];

  return {
    expanded: ['stdiolink_service', ...baseArgs, ...renderCliArgs(config, '--config.')].join(' '),
    configFile: ['stdiolink_service', ...baseArgs, `--config-file=${quote(configFileName)}`].join(' '),
  };
}

export function buildProjectConfigExportFileName(projectId: string): string {
  return `${projectId}.config.json`;
}
