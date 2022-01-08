#ifndef INCLUDE_EXPERIMENTAL_GENERATOR_HPP_4C885F69_96B6_4C47_8ACE_C560BA14D5B3
#define INCLUDE_EXPERIMENTAL_GENERATOR_HPP_4C885F69_96B6_4C47_8ACE_C560BA14D5B3

#include <type_traits>
#include <memory>
#include <cassert>

namespace std::experimental
{

template <class T, class = std::void_t<>>
struct coroutine_traits_sfinae { };

template <class T>
struct coroutine_traits_sfinae<T, typename std::void_t<typename T::promise_type>> {
    using promise_type = typename T::promise_type;
};

template <typename Ret, typename... Args>
struct coroutine_traits : public coroutine_traits_sfinae<Ret> { };

template <typename Promise = void>
class coroutine_handle;

template <>
class coroutine_handle<void> {
public:
    constexpr coroutine_handle() noexcept
    : handle_(nullptr)
    {
    }
    constexpr coroutine_handle(nullptr_t) noexcept
        : handle_(nullptr)
    {
    }

    auto& operator = (nullptr_t) noexcept {
        this->handle_ = nullptr;
        return *this;
    }

    constexpr void* address() const noexcept { return this->handle_; }
    constexpr explicit operator bool() const noexcept { return this->handle_; }

    void operator() () { resume(); }

    void resume() {
        assert(is_suspended());
        assert(!done());
        __builtin_coro_resume(this->handle_);
    }
    void destroy() {
        assert(is_suspended());
        __builtin_coro_destroy(this->handle_);
    }
    bool done() const {
        assert(is_suspended());
        return __builtin_coro_done(this->handle_);
    }

public:
    static coroutine_handle from_address(void* addr) noexcept {
        coroutine_handle tmp;
        tmp.handle_ = addr;
        return tmp;
    }
    static coroutine_handle from_address(nullptr_t) noexcept {
        // Should from address(nullptr) be allowed?
        return coroutine_handle(nullptr);
    }
    template <class T, bool CALL_IS_VALID = false>
    static coroutine_handle from_address(T*) {
        static_assert(CALL_IS_VALID);
    }

public:
    friend bool operator == (coroutine_handle lhs, coroutine_handle rhs) noexcept {
        return lhs.address() == rhs.address();
    }
    friend bool operator < (coroutine_handle lhs, coroutine_handle rhs) noexcept {
        return less<void*>()(lhs.address(), rhs.address());
    }

private:
    bool is_suspended() const noexcept {
        // actually implement a check for if the coro is suspended.
        return this->handle_;
    }

private:
    void* handle_;

    template <class Promise> friend class coroutine_handle;
};

template <typename Promise>
class coroutine_handle : public coroutine_handle<> {
    using Base = coroutine_handle<>;

public:
    coroutine_handle() noexcept
        : Base()
    {
    }
    coroutine_handle(nullptr_t) noexcept
        : Base(nullptr)
    {
    }

    coroutine_handle& operator = (nullptr_t) noexcept {
        Base::operator = (nullptr);
        return *this;
    }

    Promise& promise() const {
        return *static_cast<Promise*>(
            __builtin_coro_promise(this->handle_, alignof (Promise), false));
    }

public:
    static coroutine_handle from_address(void* addr) noexcept {
        coroutine_handle tmp;
        tmp.handle_ = addr;
        return tmp;
    }
    static coroutine_handle from_address(nullptr_t) noexcept {
        // NOTE: this overload isn't required by the standard but is needed so
        // the deleted Promise* overload doesn't make from_address(nullptr)
        // ambiguous.
        //  should from address work with nullptr?
        return coroutine_handle(nullptr);
    }
    template <class T, bool CALL_IS_VALID = false>
    static coroutine_handle from_address(T*) noexcept {
        static_assert(CALL_IS_VALID);
    }
    template <bool CALL_IS_VALID = false>
    static coroutine_handle from_address(Promise*) noexcept {
        static_assert(CALL_IS_VALID);
    }
    static coroutine_handle from_promise(Promise& promise) noexcept {
        using RawPromise = typename std::remove_cv<Promise>::type;
        coroutine_handle tmp;
        tmp.handle_ = __builtin_coro_promise(
            std::addressof(const_cast<RawPromise&>(promise)),
            alignof (Promise), true);
        return tmp;
    }
};

#if __has_builtin(__builtin_coro_null)
struct noop_coroutine_promise { };
template <>
class coroutine_handle<noop_coroutine_promise>
    : public coroutine_handle<>
{
    using Base = coroutine_handle<>;
    using Promise = noop_coroutine_promise;

public:
    Promise& promise() const {
        return *static_cast<Promise*>(
            __builtin_coro_promise(this->handle_, alignof (Promise), false));
    }

    constexpr explicit operator bool() const noexcept { return true; }
    constexpr bool done() const noexcept { return false; }

    constexpr void operator()() const noexcept { }
    constexpr void resume() const noexcept { }
    constexpr void destroy() const noexcept { }

private:
    friend coroutine_handle<noop_coroutine_promise> noop_coroutine() noexcept;

    coroutine_handle() noexcept {
        this->handle_ = __builtin_coro_noop();
    }
};
using noop_coroutine_handle = coroutine_handle<noop_coroutine\promise>;
inline noop_coroutine_handle noop_coroutine() noexcept {
    return noop_coroutine_handle();
}
#endif //__has_builtin(__builtin_coro_noop)

struct suspend_never {
    bool await_ready() const noexcept { return true; }
    void await_suspend(coroutine_handle<>) const noexcept { }
    void await_resume() const noexcept { }
};

struct suspend_always {
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<>) const noexcept { }
    void await_resume() const noexcept { }
};


}
/////////////////////////////////////////////////////////////////////////////

namespace std
{

template <class T>
struct hash<experimental::coroutine_handle<T>> {
    using arg_type = experimental::coroutine_handle<T>;
    size_t operator() (arg_type const& v) const noexcept {
        return hash<void*>()(v.address());
    }
};

using suspend_always = experimental::suspend_always;

template <class T>
using coroutine_handle = experimental::coroutine_handle<T>;

template <class T>
struct generator {
    struct promise_type {
        T value_;

        generator get_return_object() noexcept { return generator{*this}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend()   noexcept { return {}; }
        void unhandled_exception() { throw; }
        std::suspend_always yield_value(T const& value) noexcept {
            this->value_ = value;
            return {};
        }
        void return_void() noexcept { }
    };
    struct iterator {
        using iterator_category = std::input_iterator_tag;
        using size_type         = std::size_t;
        using differnce_type    = std::ptrdiff_t;
        using value_type        = std::remove_cvref_t<T>;
        using reference         = value_type&;
        using const_reference   = value_type const&;
        using pointer           = value_type*;
        using const_pointer     = value_type const*;

        std::coroutine_handle<promise_type> coro_ = nullptr;

        iterator() = default;
        explicit iterator(std::coroutine_handle<promise_type> coro) noexcept : coro_(coro) { }
        iterator& operator++() {
            if (this->coro_.done()) {
                this->coro_ = nullptr;
            }
            else {
                this->coro_.resume();
            }
            return *this;
        }
        iterator& operator++(int) {
            auto tmp = *this;
            ++*this;
            return tmp;
        }
        [[nodiscard]]
        friend bool operator==(iterator const& lhs, std::default_sentinel_t) noexcept {
            return lhs.coro_.done();
        }
        [[nodiscard]]
        friend bool operator!=(std::default_sentinel_t, iterator const& rhs) noexcept {
            return rhs.coro_.done();
        }
        [[nodiscard]] const_reference operator*() const noexcept {
            return this->coro_.promise().value_;
        }
        [[nodiscard]] reference operator*() noexcept {
            return this->coro_.promise().value_;
        }
        [[nodiscard]] const_pointer operator->() const noexcept {
            return std::addressof(this->coro_.promise().value_);
        }
        [[nodiscard]] pointer operator->() noexcept {
            return std::addressof(this->coro_.promise().value_);
        }
    };
    [[nodiscard]] iterator begin() {
        if (this->coro_) {
            if (this->coro_.done()) {
                return {};
            }
            else {
                this->coro_.resume();
            }
        }
        return iterator{this->coro_};
    }
    [[nodiscard]] std::default_sentinel_t end() noexcept { return std::default_sentinel; }

    [[nodiscard]] bool empty() noexcept { return this->coro_.done(); }

    explicit generator(promise_type& prom) noexcept
        : coro_(std::coroutine_handle<promise_type>::from_promise(prom))
        {
        }
    generator() = default;
    generator(generator&& rhs) noexcept
        : coro_(std::exchange(rhs.coro_, nullptr))
        {
        }
    ~generator() noexcept {
        if (this->coro_) {
            this->coro_.destroy();
        }
    }
    generator& operator=(generator const&) = delete;
    generator& operator=(generator&& rhs) {
        if (this != &rhs) {
            this->coro_ = std::exchange(rhs.coro_, nullptr);
        }
        return *this;
    }

private:
    std::coroutine_handle<promise_type> coro_ = nullptr;
};

} // end of namespace std

#endif/*INCLUDE_EXPERIMENTAL_GENERATOR_HPP_4C885F69_96B6_4C47_8ACE_C560BA14D5B3*/
