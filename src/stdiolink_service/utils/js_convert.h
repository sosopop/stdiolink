/// @file js_convert.h
/// @brief QJson 与 JSValue 的双向转换工具函数

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <quickjs.h>

/// @brief 将 QJsonValue 转换为 JSValue
/// @param ctx QuickJS 上下文
/// @param val 源 QJsonValue（支持 Bool、Double、String、Array、Object、Null）
/// @return 对应的 JSValue，调用方负责释放
JSValue qjsonToJsValue(JSContext* ctx, const QJsonValue& val);

/// @brief 将 QJsonObject 转换为 JS 对象
/// @param ctx QuickJS 上下文
/// @param obj 源 QJsonObject
/// @return 对应的 JS Object，调用方负责释放
JSValue qjsonObjectToJsValue(JSContext* ctx, const QJsonObject& obj);

/// @brief 将 JSValue 转换为 QJsonValue
/// @param ctx QuickJS 上下文
/// @param val 源 JSValue（不获取所有权）
/// @return 对应的 QJsonValue
QJsonValue jsValueToQJson(JSContext* ctx, JSValueConst val);

/// @brief 将 JS 对象转换为 QJsonObject
/// @param ctx QuickJS 上下文
/// @param val 源 JS Object（不获取所有权）
/// @return 对应的 QJsonObject
QJsonObject jsValueToQJsonObject(JSContext* ctx, JSValueConst val);
