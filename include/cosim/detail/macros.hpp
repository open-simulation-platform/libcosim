/**
 *  \file
 *  General utility macros.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_DETAIL_MACROS_HPP
#define COSIM_DETAIL_MACROS_HPP

/**
 *  \def COSIM_DETAIL_DEFINE_BITWISE_ENUM_OPERATORS(EnumName, BaseType)
 *  Allows the use of an `enum class` type as a bitmask type.
 *
 *  \param EnumName The `enum class` type.
 *  \param BaseType The base type, usually `int`.
 */
#define COSIM_DETAIL_DEFINE_BITWISE_ENUM_OPERATORS(EnumName, BaseType)                                          \
    constexpr EnumName operator|(EnumName a, EnumName b) noexcept                                             \
    {                                                                                                         \
        return static_cast<EnumName>(static_cast<BaseType>(a) | static_cast<BaseType>(b));                    \
    }                                                                                                         \
    constexpr EnumName operator&(EnumName a, EnumName b) noexcept                                             \
    {                                                                                                         \
        return static_cast<EnumName>(static_cast<BaseType>(a) & static_cast<BaseType>(b));                    \
    }                                                                                                         \
    constexpr EnumName& operator|=(EnumName& a, EnumName b) noexcept                                          \
    {                                                                                                         \
        return *reinterpret_cast<EnumName*>(&(*reinterpret_cast<BaseType*>(&a) |= static_cast<BaseType>(b))); \
    }                                                                                                         \
    constexpr EnumName& operator&=(EnumName& a, EnumName b) noexcept                                          \
    {                                                                                                         \
        return *reinterpret_cast<EnumName*>(&(*reinterpret_cast<BaseType*>(&a) &= static_cast<BaseType>(b))); \
    }                                                                                                         \
    constexpr bool operator!(EnumName a) { return !static_cast<BaseType>(a); }

#endif // header guard
