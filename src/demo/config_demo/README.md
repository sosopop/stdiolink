# Config Demo (M28)

M28 配置功能综合演示，展示 `stdiolink_service` 的类型安全配置系统。

构建后位于 `build/bin/config_demo/`。

## 构建

```powershell
build.bat
```

## 场景说明

| 服务目录 | 说明 |
|----------|------|
| basic_types | 基础类型（string/int/double/bool/any）+ 默认值填充 |
| constraints | 约束校验（min/max, minLength/maxLength, pattern, maxItems） |
| nested_object | 嵌套对象配置 + CLI 嵌套路径覆盖 |
| array_and_enum | 数组与枚举类型 |
| config_file_merge | 配置文件加载 + CLI > file > default 优先级合并 |
| readonly_and_errors | 只读冻结对象 + 错误处理 |

## 逐个运行

从 `build/bin` 目录执行：

```powershell
cd build/bin

# 场景1：基础类型
./stdiolink_service.exe config_demo/services/basic_types --config.name=myApp --config.port=8080

# 场景2：约束校验
./stdiolink_service.exe config_demo/services/constraints --config.port=8080 --config.name=myService

# 场景3：嵌套对象
./stdiolink_service.exe config_demo/services/nested_object --config.server.port=8080 --config.database.name=mydb

# 场景4：数组与枚举
./stdiolink_service.exe config_demo/services/array_and_enum --config.level=3 --config.mode=fast

# 场景5：文件 + CLI 合并
./stdiolink_service.exe config_demo/services/config_file_merge --config-file=config_demo/services/config/sample_config.json

# 场景5：CLI 覆盖文件值
./stdiolink_service.exe config_demo/services/config_file_merge --config-file=config_demo/services/config/sample_config.json --config.port=9999 --config.name=cli-override

# 场景6：只读 + 错误处理
./stdiolink_service.exe config_demo/services/readonly_and_errors
```

## 一键运行全部

```powershell
cd build/bin
./stdiolink_service.exe config_demo/services/all_in_one
```

## 导出 Schema

任意服务目录均可用 `--dump-config-schema` 导出 JSON schema：

```powershell
./stdiolink_service.exe config_demo/services/basic_types --dump-config-schema
```

## 错误场景验证

以下命令应报错退出：

```powershell
# 缺少必填字段 name 和 port
./stdiolink_service.exe config_demo/services/basic_types

# 端口超出范围
./stdiolink_service.exe config_demo/services/constraints --config.port=99999 --config.name=myService

# 名称不符合 pattern
./stdiolink_service.exe config_demo/services/constraints --config.port=8080 --config.name=123bad
```
