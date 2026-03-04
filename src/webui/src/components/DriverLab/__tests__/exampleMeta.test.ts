import { describe, expect, it } from 'vitest';
import { normalizeCommandExamples } from '../exampleMeta';

describe('normalizeCommandExamples', () => {
  it('returns empty array for empty input', () => {
    expect(normalizeCommandExamples(undefined)).toEqual([]);
    expect(normalizeCommandExamples(null)).toEqual([]);
    expect(normalizeCommandExamples({})).toEqual([]);
  });

  it('filters invalid example entries', () => {
    const input = [
      { description: '', mode: 'stdio', params: {} },
      { description: 'ok', mode: '', params: {} },
      { description: 'ok', mode: 'stdio', params: 1 },
      { description: 'ok', mode: 'console', params: { a: 1 } },
    ];
    const out = normalizeCommandExamples(input);
    expect(out).toEqual([{ description: 'ok', mode: 'console', params: { a: 1 } }]);
  });

  it('keeps expectedOutput when provided', () => {
    const out = normalizeCommandExamples([
      {
        description: 'demo',
        mode: 'stdio',
        params: { host: '127.0.0.1' },
        expectedOutput: { ok: true },
      },
    ]);
    expect(out).toHaveLength(1);
    expect(out[0].expectedOutput).toEqual({ ok: true });
  });
});

