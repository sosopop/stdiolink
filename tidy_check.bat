@echo off
chcp 65001
setlocal enabledelayedexpansion

:: 遍历src目录下所有的.cpp文件
for /r ".\src\" %%f in (*.cpp) do (
    :: 检查文件是否存在
    if exist "%%f" (
        echo Running clang-tidy on %%f
        :: 运行clang-tidy命令，如果失败则退出
        clang-tidy --config-file=.clang-tidy -p build_ninja --fix "%%f"
        if !errorlevel! neq 0 (
            echo Error: clang-tidy failed on %%f
            exit /b !errorlevel!
        )
    )
)

:: 遍历src目录下所有的.h文件
for /r ".\src\" %%f in (*.h) do (
    :: 检查文件是否存在
    if exist "%%f" (
        echo Running clang-tidy on %%f
        :: 运行clang-tidy命令，如果失败则退出
        clang-tidy --config-file=.clang-tidy -p build_ninja --fix "%%f"
        if !errorlevel! neq 0 (
            echo Error: clang-tidy failed on %%f
            exit /b !errorlevel!
        )
    )
)

endlocal
