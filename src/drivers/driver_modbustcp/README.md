# ModbusTCP Test

driver_modbustcp driver project.

## 构建说明

本项目使用 CMake 构建，并依赖于 `stdiolink` 库和 `Qt5/6 Core`。

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 使用说明

本项目生成的 Driver 支持 StdIO 和 Console 模式。

### Console 模式调试

```bash
./bin/driver_modbustcp --help
./bin/driver_modbustcp hello --name="World"
```
