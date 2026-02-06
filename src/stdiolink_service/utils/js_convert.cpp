#include "js_convert.h"

#include <QJsonArray>

namespace {

JSValue qjsonArrayToJsValue(JSContext* ctx, const QJsonArray& arr) {
    JSValue jsArr = JS_NewArray(ctx);
    for (int i = 0; i < arr.size(); ++i) {
        JS_SetPropertyUint32(ctx, jsArr, static_cast<uint32_t>(i), qjsonToJsValue(ctx, arr[i]));
    }
    return jsArr;
}

QJsonArray jsArrayToQJson(JSContext* ctx, JSValueConst val) {
    QJsonArray arr;
    JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    for (uint32_t i = 0; i < len; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, val, i);
        arr.append(jsValueToQJson(ctx, item));
        JS_FreeValue(ctx, item);
    }
    return arr;
}

} // namespace

JSValue qjsonToJsValue(JSContext* ctx, const QJsonValue& val) {
    switch (val.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            return JS_NULL;
        case QJsonValue::Bool:
            return JS_NewBool(ctx, val.toBool() ? 1 : 0);
        case QJsonValue::Double:
            return JS_NewFloat64(ctx, val.toDouble());
        case QJsonValue::String: {
            const QByteArray utf8 = val.toString().toUtf8();
            return JS_NewStringLen(ctx, utf8.constData(), static_cast<size_t>(utf8.size()));
        }
        case QJsonValue::Array:
            return qjsonArrayToJsValue(ctx, val.toArray());
        case QJsonValue::Object:
            return qjsonObjectToJsValue(ctx, val.toObject());
    }
    return JS_NULL;
}

JSValue qjsonObjectToJsValue(JSContext* ctx, const QJsonObject& obj) {
    JSValue jsObj = JS_NewObject(ctx);
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        JS_SetPropertyStr(ctx, jsObj, it.key().toUtf8().constData(), qjsonToJsValue(ctx, it.value()));
    }
    return jsObj;
}

QJsonValue jsValueToQJson(JSContext* ctx, JSValueConst val) {
    if (JS_IsUndefined(val) || JS_IsNull(val)) {
        return QJsonValue();
    }
    if (JS_IsBool(val)) {
        return QJsonValue(JS_ToBool(ctx, val) == 1);
    }
    if (JS_IsNumber(val)) {
        double n = 0.0;
        JS_ToFloat64(ctx, &n, val);
        return QJsonValue(n);
    }
    if (JS_IsString(val)) {
        const char* s = JS_ToCString(ctx, val);
        QString out;
        if (s) {
            out = QString::fromUtf8(s);
            JS_FreeCString(ctx, s);
        }
        return QJsonValue(out);
    }
    if (JS_IsArray(val)) {
        return QJsonValue(jsArrayToQJson(ctx, val));
    }
    if (JS_IsObject(val)) {
        return QJsonValue(jsValueToQJsonObject(ctx, val));
    }
    return QJsonValue();
}

QJsonObject jsValueToQJsonObject(JSContext* ctx, JSValueConst val) {
    QJsonObject out;
    if (!JS_IsObject(val)) {
        return out;
    }

    JSPropertyEnum* props = nullptr;
    uint32_t propCount = 0;
    if (JS_GetOwnPropertyNames(ctx, &props, &propCount, val, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) {
        return out;
    }

    for (uint32_t i = 0; i < propCount; ++i) {
        const char* key = JS_AtomToCString(ctx, props[i].atom);
        if (!key) {
            continue;
        }
        JSValue propVal = JS_GetProperty(ctx, val, props[i].atom);
        out.insert(QString::fromUtf8(key), jsValueToQJson(ctx, propVal));
        JS_FreeValue(ctx, propVal);
        JS_FreeCString(ctx, key);
    }

    JS_FreePropertyEnum(ctx, props, propCount);
    return out;
}
