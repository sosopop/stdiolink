#pragma once

struct JSContext;

class ConsoleBridge {
public:
    static void install(JSContext* ctx);
};

