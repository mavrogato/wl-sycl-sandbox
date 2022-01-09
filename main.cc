
#include <concepts>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <tuple>
#include <string_view>

#include <cxxabi.h>

#include <wayland-client.h>

#include "experimental-generator.hpp"

template <class T>
inline std::string demangled_name = [] {
    char const* name = typeid (T).name();
    int ret;
    size_t len;
    abi::__cxa_demangle(name, nullptr, &len, &ret);
    if (0 == ret) {
        std::vector<char> buf(len);
        abi::__cxa_demangle(name, buf.data(), &len, &ret);
        if (0 == ret) {
            return std::string(buf.data());
        }
    }
    return std::string(name);
}();

template <class WL_TYPE> constexpr void* wl_interface_ptr = nullptr;
template <> constexpr wl_interface const* wl_interface_ptr<wl_registry> = &wl_registry_interface;
template <> constexpr wl_interface const* wl_interface_ptr<wl_compositor> = &wl_compositor_interface;
template <> constexpr wl_interface const* wl_interface_ptr<wl_shell> = &wl_shell_interface;
template <> constexpr wl_interface const* wl_interface_ptr<wl_seat> = &wl_seat_interface;
template <> constexpr wl_interface const* wl_interface_ptr<wl_shm> = &wl_shm_interface;

template <class T>
concept wl_type = std::same_as<decltype (wl_interface_ptr<T>), wl_interface const *const>;

template <wl_type T, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, T const* ptr) {
    return output << (void*)ptr << '[' << demangled_name<T*> << ']';
}
template <wl_type T, class D, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, std::unique_ptr<T, D> const& ptr) {
    return output << ptr.get() << '[' << demangled_name<T*> << ']';
}

template <class T, class D> requires std::invocable<D, T*>
auto attach_unique(T* ptr, D deleter) noexcept {
    assert(ptr);
    return std::unique_ptr<T, D>(ptr, deleter);
}
template <wl_type T>
auto attach_unique(T* ptr) noexcept {
    assert(ptr);
    constexpr static auto deleter = [](wl_type auto* ptr) noexcept {
        std::cout << ptr << '[' << demangled_name<decltype (ptr)> << "] deleting..." << std::endl;
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(ptr));
    };
    return std::unique_ptr<T, decltype (deleter)>(ptr, deleter);
}
inline auto attach_unique(wl_display* ptr) noexcept {
    assert(ptr);
    constexpr static auto deleter = [](wl_display* ptr) noexcept {
        std::cout << ptr << '[' << demangled_name<decltype (ptr)> << "] deleting..." << std::endl;
        wl_display_disconnect(ptr);
    };
    return std::unique_ptr<wl_display, decltype (deleter)>(ptr, deleter);
}

template <size_t N>
void register_global_callback(void* data,
                              wl_registry* registry,
                              uint32_t id,
                              char const* interface,
                              uint32_t version) noexcept
{
}

template <size_t N, class First, class... Rest>
void register_global_callback(void* data,
                              wl_registry* registry,
                              uint32_t id,
                              char const* interface,
                              uint32_t version) noexcept
{
    if constexpr (N == 0) {
            std::cout << interface << " (ver." << version << ") found";
        }

    constexpr auto interface_ptr = wl_interface_ptr<First>;
    auto ret = reinterpret_cast<First**>(data) + N;
    if (std::string_view(interface) == interface_ptr->name) {
        *ret = (First*) wl_registry_bind(registry,
                                         id,
                                         interface_ptr,
                                         version);
        std::cout << "  ==> registered at " << *ret;
    }
    else {
        register_global_callback<N+1, Rest...>(data,
                                               registry,
                                               id,
                                               interface,
                                               version);
    }

    if constexpr (N == 0) {
            std::cout << std::endl;
        }
}

namespace tentative_solution {

template <typename Tuple, size_t... Is>
auto transform_each_impl(Tuple t, std::index_sequence<Is...>) {
    return std::make_tuple(
        attach_unique(std::get<Is>(t))...
    );
}

template <typename... Args>
auto transform_each(const std::tuple<Args...>& t) {
    return detail::transform_each_impl(
        t, std::make_index_sequence<sizeof...(Args)>{});
}

} // end of namespacde tentative_solution

template <class... Args>
auto register_global(wl_display* display) noexcept {
    auto registry = attach_unique(wl_display_get_registry(display));
    std::tuple<Args*...> result;
    wl_registry_listener listener = {
        .global = register_global_callback<0, Args...>,
    };
    wl_registry_add_listener(registry.get(), &listener, &result);
    wl_display_roundtrip(display);
    return transform_each(result);
}

int main() {
    try {
        auto display = attach_unique(wl_display_connect(nullptr));
        auto [compositor, shell, seat, shm] = register_global<wl_compositor,
                                                              wl_shell,
                                                              wl_seat,
                                                              wl_shm>(display.get());
        return 0;
    }
    catch (std::exception& ex) {
        std::cerr << demangled_name<decltype (ex)> << ':' << ex.what() << std::endl;
    }
    return -1;
}
