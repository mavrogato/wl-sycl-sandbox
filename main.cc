
#include <concepts>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <tuple>
#include <string_view>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

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
#define INTERN_WL_INTERFACE(wl_type) \
    template <> constexpr wl_interface const* wl_interface_ptr<wl_type> = &wl_type##_interface;
INTERN_WL_INTERFACE(wl_registry);
INTERN_WL_INTERFACE(wl_compositor);
INTERN_WL_INTERFACE(wl_shell);
INTERN_WL_INTERFACE(wl_seat);
INTERN_WL_INTERFACE(wl_shm);
INTERN_WL_INTERFACE(wl_surface);
INTERN_WL_INTERFACE(wl_shell_surface);
INTERN_WL_INTERFACE(wl_buffer);
INTERN_WL_INTERFACE(wl_shm_pool);

template <class T>
concept wl_type = std::same_as<decltype (wl_interface_ptr<T>), wl_interface const *const>;

template <wl_type T, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, T const* ptr) {
    return output << (void*) ptr
                  << '[' << wl_interface_ptr<T>->name << ']';
}
template <wl_type T, class D, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, std::unique_ptr<T, D> const& ptr) {
    return output << ptr.get() << '[' << wl_interface_ptr<T>->name << ']';
}

template <class T, class D> requires std::invocable<D, T*>
auto attach_unique(T* ptr, D deleter) noexcept {
    assert(ptr);
    return std::unique_ptr<T, D>(ptr, deleter);
}
template <wl_type T>
auto attach_unique(T* ptr) noexcept {
    assert(ptr);
    std::cout << ptr << '[' << wl_interface_ptr<T>->name << "] attaching..." << std::endl;
    constexpr static auto deleter = [](wl_type auto* ptr) noexcept {
        std::cout << ptr << '[' << wl_interface_ptr<T>->name << "] deleting..." << std::endl;
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(ptr));
    };
    return std::unique_ptr<T, decltype (deleter)>(ptr, deleter);
}
inline auto attach_unique(wl_display* ptr) noexcept {
    assert(ptr);
    std::cout << ptr << '[' << wl_display_interface.name << "] attaching..." << std::endl;
    constexpr static auto deleter = [](wl_display* ptr) noexcept {
        std::cout << ptr << '[' << wl_display_interface.name << "] deleting..." << std::endl;
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
    if constexpr (N == 0)
                 {
                     std::cout << interface << " (ver." << version << ") found";
                 }

    constexpr auto interface_ptr = wl_interface_ptr<First>;
    auto ret = reinterpret_cast<First**>(data) + N;
    if (std::string_view(interface) == interface_ptr->name) {
        *ret = (First*) wl_registry_bind(registry,
                                         id,
                                         interface_ptr,
                                         1); //version);
        if (std::string_view(interface) == "wl_shm") {
            std::cout << std::endl;
            std::cout << "-------------------------" << std::endl;
            static constexpr wl_shm_listener listener {
                .format = [](void*, wl_shm*, uint32_t format) noexcept {
                    std::cerr << "format: " << format << std::endl;
                },
            };
            auto res = wl_shm_add_listener(reinterpret_cast<wl_shm*>(*ret), &listener, nullptr);
            std::cout << demangled_name<First*> << std::endl;
            std::cout << res << std::endl;
            std::cout << "-------------------------" << std::endl;
            std::cout << std::endl;
        }
        std::cout << "  ==> registered at " << *ret;
    }
    else {
        register_global_callback<N-1, Rest...>(data,
                                               registry,
                                               id,
                                               interface,
                                               version);
    }

    if constexpr (N == 0)
                 {
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
    return transform_each_impl(
        t, std::make_index_sequence<sizeof...(Args)>{});
}

} // end of namespacde tentative_solution

template <class... Args>
auto register_global(wl_display* display) noexcept {
    auto registry = attach_unique(wl_display_get_registry(display));
    std::tuple<Args*...> result;
    wl_registry_listener listener = {
        .global = register_global_callback<sizeof... (Args) -1, Args...>,
        .global_remove = [](auto...) noexcept { },
    };
    wl_registry_add_listener(registry.get(), &listener, &result);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);
    return tentative_solution::transform_each(result);
}

// int main() {
//     try {
//         char const* XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");
//         if (XDG_RUNTIME_DIR) {
//             std::cout << "XDG_RUNTIME_DIR: " << XDG_RUNTIME_DIR << std::endl;
//         }
//         else {
//             std::cerr << "XDG_RUNTIME_DIR not found!" << std::endl;
//             return -1;
//         }

//         auto display = attach_unique(wl_display_connect(nullptr));
//         auto [compositor, shell, seat, shm] = register_global<wl_compositor,
//                                                               wl_shell,
//                                                               wl_seat,
//                                                               wl_shm>(display.get());
//         {
//             wl_shm_listener listener {
//                 .format = [](void*, wl_shm*, uint32_t format) noexcept {
//                     std::cerr << "format: " << format << std::endl;
//                 },
//             };
//             wl_shm_add_listener(shm.get(), &listener, nullptr);
//             wl_display_dispatch(display.get());
//             wl_display_roundtrip(display.get());
//         }
//         auto surface = attach_unique(wl_compositor_create_surface(compositor.get()));
//         auto shell_surface = attach_unique(wl_shell_get_shell_surface(shell.get(), surface.get()));
//         {
//             wl_shell_surface_set_toplevel(shell_surface.get());
//             wl_shell_surface_listener listener {
//                 .ping = [](void*,
//                            wl_shell_surface* shell_surface,
//                            uint32_t serial) noexcept
//                 {
//                     wl_shell_surface_pong(shell_surface, serial);
//                     std::cerr << "pinged and ponged." << std::endl;
//                 },
//                 .configure  = [](auto...) noexcept { },
//                 .popup_done = [](auto...) noexcept { },
//             };
//             wl_shell_surface_add_listener(shell_surface.get(),
//                                           &listener,
//                                           nullptr);
//             {
//                 static constexpr std::string_view pattern = "/weston-shared-XXXXXX";
//                 std::string tmpname(XDG_RUNTIME_DIR);
//                 tmpname += pattern;
//                 int fd = mkostemp(tmpname.data(), O_CLOEXEC);
//                 if (fd >= 0) {
//                     unlink(tmpname.c_str());
//                 }
//                 else {
//                     return -1; // exit
//                 }
//                 if (ftruncate(fd, 4 * 640 * 480) < 0) {
//                     close(fd);
//                     return -1;
//                 }
//                 std::cout << "***" << fd << std::endl;
//                 void* data = mmap(nullptr, 4 * 640 * 480, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
//                 if (data == MAP_FAILED) {
//                     close(fd);
//                     return -1;
//                 }
//                 std::cout << "***" << data << std::endl;
//                 auto pool = attach_unique(wl_shm_create_pool(shm.get(), fd, 4 * 640 * 480));
//                 auto buff = attach_unique(wl_shm_pool_create_buffer(pool.get(),
//                                                                     0,
//                                                                     640, 480,
//                                                                     640 * 4,
//                                                                     WL_SHM_FORMAT_XRGB8888));
//                 wl_surface_attach(surface.get(), buff.get(), 0, 0);
//                 wl_surface_commit(surface.get());
//                 pool = nullptr;
//                 std::cout << "*** pool deleted." << std::endl;

//                 uint32_t* pixel = (uint32_t*) data;
//                 for (int i = 0; i < 640 * 480; ++i) {
//                     *pixel++ = 0xffff;
//                 }

//                 while (wl_display_dispatch(display.get()) != -1) continue ;
//             }
//         }
//         return 0;
//     }
//     catch (std::exception& ex) {
//         std::cerr << demangled_name<decltype (ex)> << ':' << ex.what() << std::endl;
//     }
//     return -1;
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
//#include <wayland-server.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>


// struct wl_display *display = NULL;
// struct wl_compositor *compositor = NULL;
// struct wl_surface *surface;
// struct wl_shell *shell;
// struct wl_shell_surface *shell_surface;
// struct wl_shm *shm;
// struct wl_buffer *buffer;

void *shm_data;

int WIDTH = 480;
int HEIGHT = 360;

static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
							uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
    fprintf(stderr, "Pinged and ponged\n");
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
		 uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
};

static int
set_cloexec_or_close(int fd)
{
        long flags;

        if (fd == -1)
                return -1;

        flags = fcntl(fd, F_GETFD);
        if (flags == -1)
                goto err;

        if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
                goto err;

        return fd;

err:
        close(fd);
        return -1;
}

static int
create_tmpfile_cloexec(char *tmpname)
{
        int fd;

#ifdef HAVE_MKOSTEMP
        fd = mkostemp(tmpname, O_CLOEXEC);
        if (fd >= 0)
                unlink(tmpname);
#else
        fd = mkstemp(tmpname);
        if (fd >= 0) {
                fd = set_cloexec_or_close(fd);
                unlink(tmpname);
        }
#endif

        return fd;
}

/*
 * Create a new, unique, anonymous file of the given size, and
 * return the file descriptor for it. The file descriptor is set
 * CLOEXEC. The file is immediately suitable for mmap()'ing
 * the given size at offset zero.
 *
 * The file should not have a permanent backing store like a disk,
 * but may have if XDG_RUNTIME_DIR is not properly implemented in OS.
 *
 * The file name is deleted from the file system.
 *
 * The file is suitable for buffer sharing between processes by
 * transmitting the file descriptor over Unix sockets using the
 * SCM_RIGHTS methods.
 */
int
os_create_anonymous_file(off_t size)
{
        static const char pattern[] = "/weston-shared-XXXXXX";
        const char *path;
        char *name;
        int fd;

        path = getenv("XDG_RUNTIME_DIR");
        if (!path) {
                errno = ENOENT;
                return -1;
        }

        name = (char*) malloc(strlen(path) + sizeof(pattern));
        if (!name)
                return -1;
        strcpy(name, path);
        strcat(name, pattern);

        fd = create_tmpfile_cloexec(name);

        free(name);

        if (fd < 0)
                return -1;

        if (ftruncate(fd, size) < 0) {
                close(fd);
                return -1;
        }

        return fd;
}

static void
paint_pixels() {
    int n;
    uint32_t *pixel = (uint32_t*) shm_data;

    fprintf(stderr, "Painting pixels\n");
    for (n =0; n < WIDTH*HEIGHT; n++) {
	*pixel++ = 0xffff;
    }
}

auto create_buffer(wl_shm* shm) {
    struct wl_shm_pool *pool;
    int stride = WIDTH * 4; // 4 bytes per pixel
    int size = stride * HEIGHT;
    int fd;
    struct wl_buffer *buff;

    fd = os_create_anonymous_file(size);
    if (fd < 0) {
	fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
		size);
	exit(1);
    }
    
    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_data == MAP_FAILED) {
	fprintf(stderr, "mmap failed: %m\n");
	close(fd);
	exit(1);
    }

    pool = wl_shm_create_pool(shm, fd, size);
    buff = wl_shm_pool_create_buffer(pool, 0,
					  WIDTH, HEIGHT,
					  stride, 	
					  WL_SHM_FORMAT_XRGB8888);
    //wl_buffer_add_listener(buffer, &buffer_listener, buffer);
    wl_shm_pool_destroy(pool);
    return attach_unique(buff);
}

// auto create_window(wl_shm* shm) {
//     auto buffer = create_buffer(shm);
//     wl_surface_attach(surface, buffer, 0, 0);
//     //wl_surface_damage(surface, 0, 0, WIDTH, HEIGHT);
//     wl_surface_commit(surface);
//     return buffer;
// }

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
    //struct display *d = data;

    //	d->formats |= (1 << format);
    fprintf(stderr, "Format %d\n", format);
}

struct wl_shm_listener shm_listener = {
	shm_format
};

// static void
// global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
// 	       const char *interface, uint32_t version)
// {
//     if (strcmp(interface, "wl_compositor") == 0) {
//         compositor = (wl_compositor*) wl_registry_bind(registry, 
//                                                        id, 
//                                                        &wl_compositor_interface, 
//                                                        1);
//     } else if (strcmp(interface, "wl_shell") == 0) {
//         shell = (wl_shell*) wl_registry_bind(registry, id,
//                                              &wl_shell_interface, 1);
//     } else if (strcmp(interface, "wl_shm") == 0) {
//         shm = (wl_shm*) wl_registry_bind(registry, id,
//                                          &wl_shm_interface, 1);
// 	wl_shm_add_listener(shm, &shm_listener, NULL);
//     }
// }

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    printf("Got a registry losing event for %d\n", id);
}

// static const struct wl_registry_listener registry_listener = {
//     global_registry_handler,
//     global_registry_remover
// };

int main(int argc, char **argv) {
    auto display = attach_unique(wl_display_connect(nullptr));
    auto [compositor, shell, seat, shm] = register_global<wl_compositor,
                                                          wl_shell,
                                                          wl_seat,
                                                          wl_shm>(display.get());
    // {
    //     wl_shm_listener listener {
    //         .format = [](void*, wl_shm*, uint32_t format) noexcept {
    //             std::cerr << "format: " << format << std::endl;
    //         },
    //     };
    //     wl_shm_add_listener(shm.get(), &listener, nullptr);
    //     wl_display_dispatch(display.get());
    //     wl_display_roundtrip(display.get());
    // }
    auto surface = attach_unique(wl_compositor_create_surface(compositor.get()));
    auto shell_surface = attach_unique(wl_shell_get_shell_surface(shell.get(), surface.get()));
    wl_shell_surface_set_toplevel(shell_surface.get());
    {
        wl_shell_surface_listener listener {
            .ping = [](void*,
                       wl_shell_surface* shell_surface,
                       uint32_t serial) noexcept
            {
                wl_shell_surface_pong(shell_surface, serial);
                std::cerr << "pinged and ponged." << std::endl;
            },
            .configure  = [](auto...) noexcept { },
            .popup_done = [](auto...) noexcept { },
        };
        wl_shell_surface_add_listener(shell_surface.get(), &listener, nullptr);
    }
    auto buffer = create_buffer(shm.get());
    wl_surface_attach(surface.get(), buffer.get(), 0, 0);
    wl_surface_commit(surface.get());
    paint_pixels();
    while (wl_display_dispatch(display.get()) != -1) continue;
    return 0;
}
