/*
 * Copyright 2018-2020 Justas Masiulis
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef JM_INLINE_SYSCALL_HPP
#define JM_INLINE_SYSCALL_HPP

#include <cstdint>

/// \brief Returns an instance of syscall_function for the given syscall id.
/// \param function_pointer A function pointer whose type matches the corresponding syscall.
/// \param syscall_id The id of the syscall specified by function_pointer.
/// \note There is no INLINE_SYSCALL_MANUAL_T because you can just write
///       jm::inline_syscall<function_type>{id}
#define INLINE_SYSCALL_MANUAL(function_pointer, syscall_id) \
    ::jm::syscall_function<decltype(function_pointer)> { syscall_id }

namespace jm {

    template<class Fn>
    class syscall_function;

    /// \brief A light wrapper around the syscall to provide some type safety.
    template<class R, class... Args>
    class syscall_function<R(Args...)> {
        std::uint32_t _id = 0;

    public:
        /// \brief Initializes the syscall with zero id
        constexpr syscall_function() noexcept = default;

        /// \brief initializes syscall function with given id
        constexpr syscall_function(std::uint32_t id) noexcept : _id(id) {}

        /// \brief Performs a syscall with the given arguments
        inline R operator()(Args... args) const noexcept;
    };

    /// \brief Hashes the given function name.
    /// \note Skips first 2 characters (Nt/Zw) to avoid creating duplicate entries.
    inline constexpr std::uint32_t hash(const char* str) noexcept;

} // namespace jm

#include "inline_syscall.inl"

#endif // JM_INLINE_SYSCALL_HPP