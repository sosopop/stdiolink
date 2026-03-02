import type { FieldMeta } from '@/types/service';

/**
 * 根据字段类型返回正确的默认值。
 * array<object> 场景下用于 handleAdd 深度初始化每个子字段，
 * 避免 int 字段初始化为 ''（空字符串）导致的后续校验类型错误。
 */
export function getDefaultItem(type?: FieldMeta['type']): unknown {
  switch (type) {
    case 'object':
      return {};
    case 'array':
      return [];
    case 'bool':
      return false;
    case 'int':
    case 'int64':
    case 'double':
      return 0;
    case 'string':
    case 'enum':
    default:
      return '';
  }
}
