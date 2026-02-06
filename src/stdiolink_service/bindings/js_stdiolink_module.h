#pragma once

struct JSContext;
struct JSModuleDef;

JSModuleDef* jsInitStdiolinkModule(JSContext* ctx, const char* name);

