import { renderCliArgs } from './cliArgs';

function quote(value: string): string {
  return JSON.stringify(value);
}

function normalizePath(value: string): string {
  return value.replace(/\\/g, '/').replace(/\/+$/, '');
}

function pathSegments(value: string): string[] {
  return normalizePath(value).split('/').filter(Boolean);
}

function baseName(value: string): string {
  const segments = pathSegments(value);
  return segments[segments.length - 1] ?? '';
}

function parentDir(value: string): string {
  const normalized = normalizePath(value);
  const index = normalized.lastIndexOf('/');
  if (index < 0) {
    return '';
  }
  return normalized.slice(0, index);
}

function isAbsolutePath(value: string): boolean {
  return /^[A-Za-z]:\//.test(value) || value.startsWith('/');
}

function relativeToRoot(path: string, root: string): string {
  const normalizedPath = normalizePath(path);
  const normalizedRoot = normalizePath(root);
  const pathParts = pathSegments(normalizedPath);
  const rootParts = pathSegments(normalizedRoot);

  if (isAbsolutePath(normalizedPath) !== isAbsolutePath(normalizedRoot)) {
    return normalizedPath;
  }

  if (rootParts.length > pathParts.length) {
    return normalizedPath;
  }

  for (let index = 0; index < rootParts.length; index += 1) {
    if (rootParts[index] !== pathParts[index]) {
      return normalizedPath;
    }
  }

  const relativeParts = pathParts.slice(rootParts.length);
  return relativeParts.length > 0 ? relativeParts.join('/') : '.';
}

function displayPath(path: string, dataRoot: string): string {
  const normalizedPath = normalizePath(path);
  const normalizedDataRoot = normalizePath(dataRoot);

  if (baseName(normalizedDataRoot) !== 'data_root') {
    return normalizedPath;
  }

  const releaseRoot = parentDir(normalizedDataRoot);
  if (!releaseRoot) {
    return normalizedPath;
  }

  return relativeToRoot(normalizedPath, releaseRoot);
}

function commandWorkingDirectory(dataRoot: string): string {
  const normalizedDataRoot = normalizePath(dataRoot);

  if (baseName(normalizedDataRoot) === 'data_root') {
    const releaseRoot = parentDir(normalizedDataRoot);
    if (releaseRoot) {
      return releaseRoot;
    }
  }

  return normalizedDataRoot;
}

export interface ProjectCommandLinesOptions {
  projectId: string;
  serviceDir: string | null;
  dataRoot: string | null;
  config: Record<string, unknown>;
}

export interface ProjectCommandLines {
  expanded: string;
  configFile: string;
  configFilePath: string;
  workingDirectory: string;
}

export function buildProjectCommandLines({
  projectId,
  serviceDir,
  dataRoot,
  config,
}: ProjectCommandLinesOptions): ProjectCommandLines {
  if (!projectId || !serviceDir || !dataRoot) {
    return { expanded: '', configFile: '', configFilePath: '', workingDirectory: '' };
  }

  const workingDirectory = commandWorkingDirectory(dataRoot);
  const displayServiceDir = relativeToRoot(serviceDir, workingDirectory);
  const displayDataRoot = relativeToRoot(dataRoot, workingDirectory);
  const configFilePath = relativeToRoot(`${normalizePath(dataRoot)}/projects/${projectId}/param.json`, workingDirectory);
  const baseArgs = [quote(displayServiceDir), `--data-root=${quote(displayDataRoot)}`];
  const prefix = `cd ${quote(workingDirectory)}\n`;

  return {
    expanded: prefix + ['stdiolink_service', ...baseArgs, ...renderCliArgs(config, '--config.')].join(' '),
    configFile: prefix + ['stdiolink_service', ...baseArgs, `--config-file=${quote(configFilePath)}`].join(' '),
    configFilePath,
    workingDirectory,
  };
}
