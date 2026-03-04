import type { CommandExampleMeta } from '@/types/driver';

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function normalizeOne(item: unknown): CommandExampleMeta | null {
  if (!isObject(item)) return null;
  const description = item.description;
  const mode = item.mode;
  const params = item.params;
  if (typeof description !== 'string' || description.trim() === '') return null;
  if (typeof mode !== 'string' || mode.trim() === '') return null;
  if (!isObject(params)) return null;
  const normalized: CommandExampleMeta = {
    description: description.trim(),
    mode,
    params,
  };
  if ('expectedOutput' in item) {
    normalized.expectedOutput = item.expectedOutput;
  }
  return normalized;
}

export function normalizeCommandExamples(input: unknown): CommandExampleMeta[] {
  if (!Array.isArray(input)) return [];
  return input.map(normalizeOne).filter((v): v is CommandExampleMeta => v !== null);
}

