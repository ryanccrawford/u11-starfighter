#pragma once
#include "raylib.h"
#include <array>
#include <cstddef>

// Fixed-size object pool: no allocations during gameplay.
template <typename T, std::size_t N>
struct Pool {
    // Dense pool: live items occupy [0, n_). Iteration cost scales with live count, not capacity.
    // Items die by setting alive=false; holes are compacted after the outermost each() returns,
    // so nested each() calls and spawns during iteration are safe. Pointers are valid within a frame only.
    std::array<T, N> items{};
    T* spawn() {
        if (n_ == N) compact();
        if (n_ == N) return nullptr;  // pool full: silently drop
        T& it = items[n_++]; it = T{}; it.alive = true; return &it;
    }
    template <typename F> void each(F&& f) {
        depth_++;
        for (std::size_t i = 0; i < n_; i++) if (items[i].alive) f(items[i]);
        if (--depth_ == 0) compact();
    }
    template <typename F> void each(F&& f) const { for (std::size_t i = 0; i < n_; i++) if (items[i].alive) f(items[i]); }
    std::size_t count() const { std::size_t n = 0; for (std::size_t i = 0; i < n_; i++) n += items[i].alive; return n; }
    void clear() { for (auto& it : items) it.alive = false; n_ = 0; }
    void compact() {
        if (depth_) return;
        for (std::size_t i = 0; i < n_;) {
            if (items[i].alive) { i++; continue; }
            items[i] = items[--n_]; items[n_].alive = false;
        }
    }
    std::size_t n_ = 0; int depth_ = 0;
};

inline float frand(float a, float b) { return a + (b - a) * (float)GetRandomValue(0, 10000) / 10000.f; }

struct Bullet { bool alive = false; Vector2 pos{}, vel{}; float r = 4; Color col = WHITE; };
struct Star { Vector2 pos{}; float speed = 0, size = 1; unsigned char bright = 255; };
