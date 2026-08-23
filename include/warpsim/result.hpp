#pragma once

#include <optional>
#include <utility>
#include <variant>

namespace warpsim {

/// Tag wrapper that carries an error into a Result, in the spirit of std::unexpected.
template <typename E>
struct Failure {
    E error;
};

template <typename E>
[[nodiscard]] constexpr Failure<E> fail(E error) noexcept {
    return Failure<E>{std::move(error)};
}

/// A value or an error. This is the C++20 stand-in for std::expected, which is
/// C++23: the project targets C++20 so that the language level is itself a
/// reviewable choice, and the subset of the interface used here is small.
template <typename T, typename E>
class Result {
public:
    constexpr Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {} // NOLINT
    constexpr Result(Failure<E> failure)                                              // NOLINT
        : storage_(std::in_place_index<1>, std::move(failure.error)) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] constexpr const T& value() const& { return std::get<0>(storage_); }
    [[nodiscard]] constexpr T& value() & { return std::get<0>(storage_); }
    [[nodiscard]] constexpr T&& value() && { return std::get<0>(std::move(storage_)); }
    [[nodiscard]] constexpr const E& error() const& { return std::get<1>(storage_); }

    [[nodiscard]] constexpr const T& operator*() const& { return value(); }
    [[nodiscard]] constexpr T& operator*() & { return value(); }
    [[nodiscard]] constexpr const T* operator->() const { return &value(); }
    [[nodiscard]] constexpr T* operator->() { return &value(); }

private:
    std::variant<T, E> storage_;
};

/// Success carries nothing; only the error is meaningful.
template <typename E>
class Result<void, E> {
public:
    constexpr Result() noexcept = default;
    constexpr Result(Failure<E> failure) : error_(std::move(failure.error)) {} // NOLINT

    [[nodiscard]] constexpr bool has_value() const noexcept { return !error_.has_value(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }
    /// Precondition: !has_value().
    [[nodiscard]] constexpr const E& error() const& {
        return *error_; // NOLINT(bugprone-unchecked-optional-access)
    }

private:
    std::optional<E> error_;
};

} // namespace warpsim
