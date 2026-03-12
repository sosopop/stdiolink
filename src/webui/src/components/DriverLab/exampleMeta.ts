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
  const out: CommandExampleMeta[] = [];
  for (const item of input) {
    const normalized = normalizeOne(item);
    if (normalized !== null) {
      out.push(normalized);
    }
  }
  return out;
}

export function selectDriverLabExamples(input: unknown): CommandExampleMeta[] {
  return normalizeCommandExamples(input).filter((example) => example.mode === 'stdio');
}
