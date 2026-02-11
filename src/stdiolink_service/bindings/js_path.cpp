#include "js_path.h"

#include <QDir>
#include <QFileInfo>
#include <quickjs.h>

namespace stdiolink_service {

namespace {

/// 统一路径分隔符为 '/'
QString normalizeSeparators(const QString& path) {
    QString result = path;
    result.replace('\\', '/');
    return result;
}

JSValue jsJoin(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc == 0) {
        return JS_NewString(ctx, ".");
    }
    QStringList segments;
    for (int i = 0; i < argc; ++i) {
        if (!JS_IsString(argv[i])) {
            return JS_ThrowTypeError(ctx, "join: argument %d must be a string", i);
        }
        const char* str = JS_ToCString(ctx, argv[i]);
        if (!str) return JS_EXCEPTION;
        segments.append(QString::fromUtf8(str));
        JS_FreeCString(ctx, str);
    }
    QString joined = segments.join('/');
    QString result = normalizeSeparators(QDir::cleanPath(joined));
    return JS_NewString(ctx, result.toUtf8().constData());
}

JSValue jsResolve(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    QString result = QDir::currentPath();
    for (int i = 0; i < argc; ++i) {
        if (!JS_IsString(argv[i])) {
            return JS_ThrowTypeError(ctx, "resolve: argument %d must be a string", i);
        }
        const char* str = JS_ToCString(ctx, argv[i]);
        if (!str) return JS_EXCEPTION;
        QString seg = QString::fromUtf8(str);
        JS_FreeCString(ctx, str);
        if (QDir::isAbsolutePath(seg)) {
            result = seg;
        } else {
            result = QDir(result).filePath(seg);
        }
    }
    return JS_NewString(ctx,
        normalizeSeparators(QDir::cleanPath(result)).toUtf8().constData());
}

JSValue jsDirname(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "dirname: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    QFileInfo fi(path);
    return JS_NewString(ctx, normalizeSeparators(fi.path()).toUtf8().constData());
}

JSValue jsBasename(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "basename: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    // 去除末尾分隔符
    while (path.endsWith('/') || path.endsWith('\\')) {
        path.chop(1);
    }
    QFileInfo fi(path);
    return JS_NewString(ctx, fi.fileName().toUtf8().constData());
}

JSValue jsExtname(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "extname: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    QFileInfo fi(path);
    QString suffix = fi.suffix();
    if (suffix.isEmpty()) {
        return JS_NewString(ctx, "");
    }
    QString result = "." + suffix;
    return JS_NewString(ctx, result.toUtf8().constData());
}

JSValue jsNormalize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "normalize: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    return JS_NewString(ctx,
        normalizeSeparators(QDir::cleanPath(path)).toUtf8().constData());
}

JSValue jsIsAbsolute(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "isAbsolute: argument must be a string");
    }
    const char* str = JS_ToCString(ctx, argv[0]);
    if (!str) return JS_EXCEPTION;
    QString path = QString::fromUtf8(str);
    JS_FreeCString(ctx, str);
    return JS_NewBool(ctx, QDir::isAbsolutePath(path));
}

int pathModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "join",
        JS_NewCFunction(ctx, jsJoin, "join", 0));
    JS_SetModuleExport(ctx, module, "resolve",
        JS_NewCFunction(ctx, jsResolve, "resolve", 0));
    JS_SetModuleExport(ctx, module, "dirname",
        JS_NewCFunction(ctx, jsDirname, "dirname", 1));
    JS_SetModuleExport(ctx, module, "basename",
        JS_NewCFunction(ctx, jsBasename, "basename", 1));
    JS_SetModuleExport(ctx, module, "extname",
        JS_NewCFunction(ctx, jsExtname, "extname", 1));
    JS_SetModuleExport(ctx, module, "normalize",
        JS_NewCFunction(ctx, jsNormalize, "normalize", 1));
    JS_SetModuleExport(ctx, module, "isAbsolute",
        JS_NewCFunction(ctx, jsIsAbsolute, "isAbsolute", 1));
    return 0;
}

} // namespace

JSModuleDef* JsPathBinding::initModule(JSContext* ctx, const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, pathModuleInit);
    if (!module) return nullptr;
    const char* exports[] = {
        "join", "resolve", "dirname", "basename",
        "extname", "normalize", "isAbsolute"
    };
    for (const char* e : exports) {
        JS_AddModuleExport(ctx, module, e);
    }
    return module;
}

} // namespace stdiolink_service
