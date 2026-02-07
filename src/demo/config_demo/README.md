# Config Demo (M28)

M28 配置功能综合演示，展示 `stdiolink_service` 的类型安全配置系统。

脚本构建后位于 `build_ninja/bin/config_demo/`。

## 构建

```powershell
build_ninja.bat
```

## 场景说明

| 脚本 | 说明 |
|------|------|
| 01_basic_types.js | 基础类型（string/int/double/bool/any）+ 默认值填充 |
| 02_constraints.js | 约束校验（min/max, minLength/maxLength, pattern, maxItems） |
| 03_nested_object.js | 嵌套对象配置 + CLI 嵌套路径覆盖 |
| 04_array_and_enum.js | 数组与枚举类型 |
| 05_config_file_merge.js | 配置文件加载 + CLI > file > default 优先级合并 |
| 06_readonly_and_errors.js | 只读冻结对象 + 重复 defineConfig 报错 |

## 逐个运行

从仓库根目录执行：

```powershell
cd build_ninja/bin

# 场景1：基础类型
./stdiolink_service.exe config_demo/01_basic_types.js --config.name=myApp --config.port=8080

# 场景2：约束校验
./stdiolink_service.exe config_demo/02_constraints.js --config.port=8080 --config.name=myService

# 场景3：嵌套对象
./stdiolink_service.exe config_demo/03_nested_object.js --config.server.port=8080 --config.database.name=mydb

# 场景4：数组与枚举
./stdiolink_service.exe config_demo/04_array_and_enum.js --config.level=3 --config.mode=fast

# 场景5：文件 + CLI 合并
./stdiolink_service.exe config_demo/05_config_file_merge.js --config-file=config_demo/config/sample_config.json

# 场景5：CLI 覆盖文件值
./stdiolink_service.exe config_demo/05_config_file_merge.js --config-file=config_demo/config/sample_config.json --config.port=9999 --config.name=cli-override

# 场景6：只读 + 错误处理
./stdiolink_service.exe config_demo/06_readonly_and_errors.js
```

## 一键运行全部

```powershell
cd build_ninja/bin
./stdiolink_service.exe config_demo/00_all_in_one.js
```

## 导出 Schema

任意带 `defineConfig()` 的脚本均可用 `--dump-config-schema` 导出 JSON schema：

```powershell
./stdiolink_service.exe config_demo/01_basic_types.js --dump-config-schema
```

## 错误场景验证

以下命令应报错退出：

```powershell
# 缺少必填字段 name 和 port
./stdiolink_service.exe config_demo/01_basic_types.js

# 端口超出范围
./stdiolink_service.exe config_demo/02_constraints.js --config.port=99999 --config.name=myService

# 名称不符合 pattern
./stdiolink_service.exe config_demo/02_constraints.js --config.port=8080 --config.name=123bad
```
