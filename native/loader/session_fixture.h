#ifndef SKINLAB_SESSION_FIXTURE_H
#define SKINLAB_SESSION_FIXTURE_H
#include <skinlab/dx11_bridge.h>
struct SlSessionFixtureConfig {uint32_t struct_size;SlConfig core;IDXGISwapChain* chain;};
using SlGetSessionFixture=int (__cdecl*)(SlSessionFixtureConfig*);
#endif
