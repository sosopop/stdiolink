#include "module_loader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include "quickjs.h"

namespace {

using BuiltinInitFn = JSModuleDef* (*)(JSContext*, const char*);

QHash<QString, BuiltinInitFn>& builtins() {
    static QHash<QString, BuiltinInitFn> s_builtins;
    return s_builtins;
}

QString normalizeSeparators(const QString& path) {
#ifdef Q_OS_WIN
    QString out = path;
    out.replace('\\', '/');
    return out;
#else
    return path;
#endif
}

bool isRelativeSpecifier(const QString& specifier) {
    return specifier.startsWith("./")
        || specifier.startsWith("../")
#ifdef Q_OS_WIN
        || specifier.startsWith(".\\")
        || specifier.startsWith("..\\")
#endif
        ;
}

bool hasSupportedExtension(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "js" || suffix == "mjs";
}

QString normalizeForCache(const QString& absPath) {
    QFileInfo info(absPath);
    QString normalized = info.canonicalFilePath();
    if (normalized.isEmpty()) {
        normalized = info.absoluteFilePath();
    }
    normalized = QDir::cleanPath(normalized);
    normalized = QDir::fromNativeSeparators(normalized);
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

QString resolveAbsolutePath(const QString& baseName, const QString& rawModuleName) {
    const QString moduleName = normalizeSeparators(rawModuleName);
    if (QDir::isAbsolutePath(moduleName)) {
        return QDir::cleanPath(QFileInfo(moduleName).absoluteFilePath());
    }

    QString baseDir;
    if (baseName.isEmpty() || builtins().contains(baseName)) {
        baseDir = QDir::currentPath();
    } else {
        baseDir = QFileInfo(baseName).absolutePath();
    }

    const QString resolved = QDir(baseDir).filePath(moduleName);
    return QDir::cleanPath(QFileInfo(resolved).absoluteFilePath());
}

} // namespace

void ModuleLoader::install(JSContext* ctx) {
    if (!ctx) {
        return;
    }
    JS_SetModuleLoaderFunc(JS_GetRuntime(ctx), &ModuleLoader::normalize, &ModuleLoader::loader, nullptr);
}

void ModuleLoader::addBuiltin(const QString& name, JSModuleDef* (*init)(JSContext*, const char*)) {
    if (name.isEmpty() || !init) {
        return;
    }
    builtins().insert(name, init);
}

char* ModuleLoader::normalize(JSContext* ctx,
                              const char* baseName,
                              const char* name,
                              void* opaque) {
    Q_UNUSED(opaque);
    if (!ctx || !name) {
        return nullptr;
    }

    const QString moduleName = QString::fromUtf8(name);
    if (builtins().contains(moduleName)) {
        return js_strdup(ctx, name);
    }

    if (!QDir::isAbsolutePath(moduleName) && !isRelativeSpecifier(moduleName)) {
        JS_ThrowReferenceError(
            ctx,
            "Unsupported bare module specifier '%s'; only builtins or relative/absolute file paths are allowed",
            name);
        return nullptr;
    }

    const QString base = baseName ? normalizeSeparators(QString::fromUtf8(baseName)) : QString();
    const QString absolutePath = resolveAbsolutePath(base, moduleName);
    const QFileInfo fileInfo(absolutePath);

    if (fileInfo.exists() && fileInfo.isDir()) {
        JS_ThrowReferenceError(
            ctx,
            "Directory import is not supported for '%s'; use an explicit file path",
            name);
        return nullptr;
    }

    if (!hasSupportedExtension(absolutePath)) {
        JS_ThrowReferenceError(
            ctx,
            "Module specifier '%s' must include an explicit .js or .mjs extension",
            name);
        return nullptr;
    }

    // Cache key must be stable across path spellings; no extension probing or index fallback.
    const QString normalized = normalizeForCache(absolutePath);
    const QByteArray utf8 = normalized.toUtf8();
    return js_strdup(ctx, utf8.constData());
}

JSModuleDef* ModuleLoader::loader(JSContext* ctx, const char* moduleName, void* opaque) {
    Q_UNUSED(opaque);
    if (!ctx || !moduleName) {
        return nullptr;
    }

    const QString name = QString::fromUtf8(moduleName);
    auto it = builtins().constFind(name);
    if (it != builtins().constEnd()) {
        return it.value()(ctx, moduleName);
    }

    QFileInfo fi(name);
    if (!fi.exists() || !fi.isFile()) {
        JS_ThrowReferenceError(ctx, "Module not found: %s", moduleName);
        return nullptr;
    }

    QFile file(name);
    if (!file.open(QIODevice::ReadOnly)) {
        JS_ThrowReferenceError(ctx, "Module not found: %s", moduleName);
        return nullptr;
    }

    const QByteArray code = file.readAll();
    file.close();

    JSValue modVal = JS_Eval(ctx, code.constData(), static_cast<size_t>(code.size()),
                             moduleName, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(modVal)) {
        return nullptr;
    }

    if (JS_VALUE_GET_TAG(modVal) != JS_TAG_MODULE) {
        JS_FreeValue(ctx, modVal);
        JS_ThrowTypeError(ctx, "Invalid module object: %s", moduleName);
        return nullptr;
    }

    return static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(modVal));
}
