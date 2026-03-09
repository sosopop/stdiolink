# Meta And Validation

## Purpose

理解 `DriverMeta` 如何描述命令/参数/返回值，以及何处执行校验。

## Key Conclusions

- `DriverMeta` 是 Driver 自描述中心，既服务文档，也服务参数校验、表单生成、配置 schema。
- C++ Driver 通过 `MetaBuilder` 构建元数据；Host、JS Service、WebUI 依赖导出的元数据消费。
- 校验链通常包含 schema 校验和默认值填充。

## Core Types

- `FieldMeta` / `FieldType`
- `CommandMeta`
- `DriverMeta`
- `ConfigSchema`

## Implementation Entry

- 类型：`src/stdiolink/protocol/meta_types.*`
- 命令参数校验：`src/stdiolink/protocol/meta_validator.*`
- schema 校验：`src/stdiolink/protocol/meta_schema_validator.*`
- Driver 端构建器：`src/stdiolink/driver/meta_builder.*`

## Downstream Consumers

- Driver `meta.describe` 导出
- Host `meta_cache.*`
- Host `form_generator.*`
- Service `config/service_config_schema.*`
- Server Driver 扫描和 WebUI DriverLab

## Modify Entry

- 改类型系统时必须同步检查：协议层测试、Host 表单生成、Service 配置 schema、Server 扫描输出。

## Related

- `../02-driver/driver-meta.md`
- `../03-host/wait-any-and-forms.md`
- `../04-service/service-config-and-driver.md`
