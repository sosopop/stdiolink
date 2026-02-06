#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include "quickjs.h"

JSValue qjsonToJsValue(JSContext* ctx, const QJsonValue& val);
JSValue qjsonObjectToJsValue(JSContext* ctx, const QJsonObject& obj);
QJsonValue jsValueToQJson(JSContext* ctx, JSValueConst val);
QJsonObject jsValueToQJsonObject(JSContext* ctx, JSValueConst val);
