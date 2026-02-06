#pragma once

#include <QString>

struct JSContext;
struct JSModuleDef;

class ModuleLoader {
public:
    static void install(JSContext* ctx);
    static void addBuiltin(const QString& name, JSModuleDef* (*init)(JSContext*, const char*));

    // Resolution rules (fixed by design):
    // 1) Builtin module names are resolved by exact name.
    // 2) Non-builtin specifiers must be relative or absolute file paths.
    // 3) Specifier must include explicit .js/.mjs extension.
    // 4) No extension probing and no directory index fallback.

private:
    static char* normalize(JSContext* ctx,
                           const char* baseName,
                           const char* name,
                           void* opaque);
    static JSModuleDef* loader(JSContext* ctx,
                               const char* moduleName,
                               void* opaque);
};
