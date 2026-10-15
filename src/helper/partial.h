#ifndef PARTIAL_H
#define PARTIAL_H

#include <functional>
#include <utility>

template<class F, class... BoundArgs>
auto partial(F &&f, BoundArgs &&... bound_args) {
    return [
        fn = std::forward<F>(f),
        ...args = std::forward<BoundArgs>(bound_args)
    ]<class... CallArgs>(CallArgs &&... call_args) mutable -> decltype(auto) {
        return std::invoke(
            fn,
            args...,
            std::forward<CallArgs>(call_args)...
        );
    };
}

#endif //PARTIAL_H