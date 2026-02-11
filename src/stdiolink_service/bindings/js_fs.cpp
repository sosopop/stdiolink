#include "js_fs.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <quickjs.h>

#include "utils/js_convert.h"

namespace stdiolink_service {

namespace {

bool extractStringArg(JSContext* ctx, JSValueConst val,
                      const char* func, int index, QString& out) {
    if (!JS_IsString(val)) {
        JS_ThrowTypeError(ctx, "fs.%s: argument %d must be a string",
                          func, index);
        return false;
    }
    const char* s = JS_ToCString(ctx, val);
    if (!s) return false;
    out = QString::fromUtf8(s);
    JS_FreeCString(ctx, s);
    return true;
}

bool optBool(JSContext* ctx, JSValueConst opts,
             const char* key, bool defaultVal) {
    JSValue v = JS_GetPropertyStr(ctx, opts, key);
    bool result = defaultVal;
    if (JS_IsBool(v)) {
        result = JS_ToBool(ctx, v);
    }
    JS_FreeValue(ctx, v);
    return result;
}

JSValue jsExists(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.exists: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "exists", 0, path))
        return JS_EXCEPTION;
    if (path.isEmpty()) {
        return JS_ThrowTypeError(ctx, "fs.exists: path must not be empty");
    }
    return JS_NewBool(ctx, QFileInfo::exists(path));
}

JSValue jsReadText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.readText: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "readText", 0, path))
        return JS_EXCEPTION;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return JS_ThrowInternalError(ctx,
            "fs.readText: cannot open file (path: %s)",
            path.toUtf8().constData());
    }
    QByteArray data = file.readAll();
    QString text = QString::fromUtf8(data.constData(), data.size());
    if (text.toUtf8() != data) {
        return JS_ThrowInternalError(ctx,
            "fs.readText: file is not valid UTF-8 (path: %s)",
            path.toUtf8().constData());
    }
    return JS_NewStringLen(ctx, data.constData(),
                           static_cast<size_t>(data.size()));
}

JSValue jsWriteText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
            "fs.writeText: path and text arguments required");
    }
    QString path, text;
    if (!extractStringArg(ctx, argv[0], "writeText", 0, path))
        return JS_EXCEPTION;
    if (!extractStringArg(ctx, argv[1], "writeText", 1, text))
        return JS_EXCEPTION;
    bool append = false, ensureParent = false;
    if (argc >= 3 && JS_IsObject(argv[2])) {
        append = optBool(ctx, argv[2], "append", false);
        ensureParent = optBool(ctx, argv[2], "ensureParent", false);
    }
    if (ensureParent) {
        QDir().mkpath(QFileInfo(path).absolutePath());
    }
    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
    if (append) mode |= QIODevice::Append;
    QFile file(path);
    if (!file.open(mode)) {
        return JS_ThrowInternalError(ctx,
            "fs.writeText: cannot open file for writing (path: %s)",
            path.toUtf8().constData());
    }
    file.write(text.toUtf8());
    return JS_UNDEFINED;
}

JSValue jsReadJson(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.readJson: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "readJson", 0, path))
        return JS_EXCEPTION;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return JS_ThrowInternalError(ctx,
            "fs.readJson: cannot open file (path: %s)",
            path.toUtf8().constData());
    }
    QByteArray data = file.readAll();
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        return JS_ThrowInternalError(ctx,
            "fs.readJson: invalid JSON: %s (path: %s)",
            parseErr.errorString().toUtf8().constData(),
            path.toUtf8().constData());
    }
    if (doc.isObject()) {
        return qjsonObjectToJsValue(ctx, doc.object());
    }
    if (doc.isArray()) {
        return qjsonToJsValue(ctx, QJsonValue(doc.array()));
    }
    return JS_NULL;
}

JSValue jsWriteJson(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx,
            "fs.writeJson: path and value arguments required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "writeJson", 0, path))
        return JS_EXCEPTION;
    bool ensureParent = false;
    if (argc >= 3 && JS_IsObject(argv[2])) {
        ensureParent = optBool(ctx, argv[2], "ensureParent", false);
    }
    if (ensureParent) {
        QDir().mkpath(QFileInfo(path).absolutePath());
    }
    QJsonValue jval = jsValueToQJson(ctx, argv[1]);
    QJsonDocument doc;
    if (jval.isArray()) {
        doc = QJsonDocument(jval.toArray());
    } else {
        doc = QJsonDocument(jval.toObject());
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return JS_ThrowInternalError(ctx,
            "fs.writeJson: cannot open file for writing (path: %s)",
            path.toUtf8().constData());
    }
    file.write(doc.toJson(QJsonDocument::Compact));
    return JS_UNDEFINED;
}

JSValue jsMkdir(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.mkdir: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "mkdir", 0, path))
        return JS_EXCEPTION;
    bool recursive = true;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        recursive = optBool(ctx, argv[1], "recursive", true);
    }
    bool ok = recursive ? QDir().mkpath(path) : QDir().mkdir(path);
    if (!ok) {
        return JS_ThrowInternalError(ctx,
            "fs.mkdir: failed to create directory (path: %s)",
            path.toUtf8().constData());
    }
    return JS_UNDEFINED;
}

JSValue jsListDir(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.listDir: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "listDir", 0, path))
        return JS_EXCEPTION;
    bool recursive = false, filesOnly = false, dirsOnly = false;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        recursive = optBool(ctx, argv[1], "recursive", false);
        filesOnly = optBool(ctx, argv[1], "filesOnly", false);
        dirsOnly = optBool(ctx, argv[1], "dirsOnly", false);
    }
    if (filesOnly && dirsOnly) {
        return JS_ThrowTypeError(ctx,
            "fs.listDir: filesOnly and dirsOnly are mutually exclusive");
    }
    QDir dir(path);
    if (!dir.exists()) {
        return JS_ThrowInternalError(ctx,
            "fs.listDir: directory does not exist (path: %s)",
            path.toUtf8().constData());
    }
    QDir::Filters filters = QDir::NoDotAndDotDot;
    if (filesOnly) filters |= QDir::Files;
    else if (dirsOnly) filters |= QDir::Dirs;
    else filters |= QDir::Files | QDir::Dirs;
    QStringList entries;
    if (recursive) {
        QDirIterator it(path, filters, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            entries.append(dir.relativeFilePath(it.filePath()));
        }
    } else {
        entries = dir.entryList(filters, QDir::Name);
    }
    entries.sort();
    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < entries.size(); ++i) {
        JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
            JS_NewString(ctx, entries[i].toUtf8().constData()));
    }
    return arr;
}

JSValue jsStat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs.stat: path argument required");
    }
    QString path;
    if (!extractStringArg(ctx, argv[0], "stat", 0, path))
        return JS_EXCEPTION;
    QFileInfo fi(path);
    if (!fi.exists()) {
        return JS_ThrowInternalError(ctx,
            "fs.stat: path does not exist (path: %s)",
            path.toUtf8().constData());
    }
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "isFile", JS_NewBool(ctx, fi.isFile()));
    JS_SetPropertyStr(ctx, obj, "isDir", JS_NewBool(ctx, fi.isDir()));
    JS_SetPropertyStr(ctx, obj, "size", JS_NewFloat64(ctx, fi.size()));
    JS_SetPropertyStr(ctx, obj, "mtimeMs",
        JS_NewFloat64(ctx, fi.lastModified().toMSecsSinceEpoch()));
    return obj;
}

int fsModuleInit(JSContext* ctx, JSModuleDef* module) {
    JS_SetModuleExport(ctx, module, "exists",
        JS_NewCFunction(ctx, jsExists, "exists", 1));
    JS_SetModuleExport(ctx, module, "readText",
        JS_NewCFunction(ctx, jsReadText, "readText", 1));
    JS_SetModuleExport(ctx, module, "writeText",
        JS_NewCFunction(ctx, jsWriteText, "writeText", 2));
    JS_SetModuleExport(ctx, module, "readJson",
        JS_NewCFunction(ctx, jsReadJson, "readJson", 1));
    JS_SetModuleExport(ctx, module, "writeJson",
        JS_NewCFunction(ctx, jsWriteJson, "writeJson", 2));
    JS_SetModuleExport(ctx, module, "mkdir",
        JS_NewCFunction(ctx, jsMkdir, "mkdir", 1));
    JS_SetModuleExport(ctx, module, "listDir",
        JS_NewCFunction(ctx, jsListDir, "listDir", 1));
    JS_SetModuleExport(ctx, module, "stat",
        JS_NewCFunction(ctx, jsStat, "stat", 1));
    return 0;
}

} // namespace

JSModuleDef* JsFsBinding::initModule(JSContext* ctx, const char* name) {
    JSModuleDef* module = JS_NewCModule(ctx, name, fsModuleInit);
    if (!module) return nullptr;
    const char* exports[] = {
        "exists", "readText", "writeText", "readJson",
        "writeJson", "mkdir", "listDir", "stat"
    };
    for (const char* e : exports) {
        JS_AddModuleExport(ctx, module, e);
    }
    return module;
}

} // namespace stdiolink_service
