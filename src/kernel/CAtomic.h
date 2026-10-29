///////////////////////////////////////////////////////////////////////////////
///
/// @author            Brian Card
/// @date              01.04.2026
///
/// @file              CAtomic.h
///
/// @brief             Compatibility with C11's stdatomic.h
///
/// @copyright
///                      Copyright (c) 2026 Brian Card
///
/// Permission is hereby granted, free of charge, to any person obtaining a
/// copy of this software and associated documentation files (the "Software"),
/// to deal in the Software without restriction, including without limitation
/// the rights to use, copy, modify, merge, publish, distribute, sublicense,
/// and/or sell copies of the Software, and to permit persons to whom the
/// Software is furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included
/// in all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
/// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
/// DEALINGS IN THE SOFTWARE.
///
///                                Brian Card
///                      https://github.com/brian-card
///
///////////////////////////////////////////////////////////////////////////////

#ifndef C_ATOMIC_H
#define C_ATOMIC_H

#if (__STDC_VERSION__ >= 201112L) && (__STDC_NO_ATOMICS__ != 1)

#include "stdatomic.h"
#define C11_ATOMIC_OK 1

#elif defined(__cplusplus)

#if defined(__has_include)
  #if __has_include(<atomic>)
    #define HAS_CXX_ATOMIC 1
  #else
    #define HAS_CXX_ATOMIC 0
  #endif
#elif __cplusplus >= 201103L
  #define HAS_CXX_ATOMIC 1
#else
  #define HAS_CXX_ATOMIC 0
#endif

#endif // __cplusplus

#if !C11_ATOMIC_OK && HAS_CXX_ATOMIC

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

// Memory order mapping
typedef std::memory_order memory_order;

// Trivially-copyable atomic wrapper for C11 compatibility
template<typename T>
struct _Atomic_storage {
  alignas(std::atomic<T>) unsigned char data[sizeof(std::atomic<T>)];
  
  std::atomic<T>* ptr() volatile {
    return reinterpret_cast<std::atomic<T>*>(const_cast<unsigned char*>(data));
  }
  
  const volatile std::atomic<T>* ptr() const volatile {
    return reinterpret_cast<const volatile std::atomic<T>*>(data);
  }
  
  // Assignment operator
  volatile _Atomic_storage& operator=(T desired) volatile {
    ptr()->store(desired, std::memory_order_seq_cst);
    return *this;
  }
  
  _Atomic_storage& operator=(T desired) {
    ptr()->store(desired, std::memory_order_seq_cst);
    return *this;
  }
  
  // Conversion operator (implicit load)
  operator T() const volatile {
    return ptr()->load(std::memory_order_seq_cst);
  }
  
  operator T() const {
    return ptr()->load(std::memory_order_seq_cst);
  }
  
  // Comparison operators
  bool operator==(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) == other;
  }
  
  bool operator!=(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) != other;
  }
  
  bool operator<(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) < other;
  }
  
  bool operator>(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) > other;
  }
  
  bool operator<=(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) <= other;
  }
  
  bool operator>=(T other) const volatile {
    return ptr()->load(std::memory_order_seq_cst) >= other;
  }
  
  // Compound assignment operators (for integral/pointer types)
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value, 
    volatile _Atomic_storage&>::type
  operator+=(T operand) volatile {
    ptr()->fetch_add(operand, std::memory_order_seq_cst);
    return *this;
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value,
    volatile _Atomic_storage&>::type
  operator-=(T operand) volatile {
    ptr()->fetch_sub(operand, std::memory_order_seq_cst);
    return *this;
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value,
    volatile _Atomic_storage&>::type
  operator&=(T operand) volatile {
    ptr()->fetch_and(operand, std::memory_order_seq_cst);
    return *this;
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value,
    volatile _Atomic_storage&>::type
  operator|=(T operand) volatile {
    ptr()->fetch_or(operand, std::memory_order_seq_cst);
    return *this;
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value,
    volatile _Atomic_storage&>::type
  operator^=(T operand) volatile {
    ptr()->fetch_xor(operand, std::memory_order_seq_cst);
    return *this;
  }
  
  // Pre-increment/decrement
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value, T>::type
  operator++() volatile {
    return ptr()->fetch_add(1, std::memory_order_seq_cst) + 1;
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value, T>::type
  operator--() volatile {
    return ptr()->fetch_sub(1, std::memory_order_seq_cst) - 1;
  }
  
  // Post-increment/decrement
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value, T>::type
  operator++(int) volatile {
    return ptr()->fetch_add(1, std::memory_order_seq_cst);
  }
  
  template<typename U = T>
  typename std::enable_if<std::is_integral<U>::value || std::is_pointer<U>::value, T>::type
  operator--(int) volatile {
    return ptr()->fetch_sub(1, std::memory_order_seq_cst);
  }
};

// Add reverse comparison operators (for when T is on the left side)
template<typename T>
inline bool operator==(T left, const volatile _Atomic_storage<T>& right) {
  return left == right.ptr()->load(std::memory_order_seq_cst);
}

template<typename T>
inline bool operator!=(T left, const volatile _Atomic_storage<T>& right) {
  return left != right.ptr()->load(std::memory_order_seq_cst);
}

template<typename T>
inline bool operator<(T left, const volatile _Atomic_storage<T>& right) {
  return left < right.ptr()->load(std::memory_order_seq_cst);
}

template<typename T>
inline bool operator>(T left, const volatile _Atomic_storage<T>& right) {
  return left > right.ptr()->load(std::memory_order_seq_cst);
}

template<typename T>
inline bool operator<=(T left, const volatile _Atomic_storage<T>& right) {
  return left <= right.ptr()->load(std::memory_order_seq_cst);
}

template<typename T>
inline bool operator>=(T left, const volatile _Atomic_storage<T>& right) {
  return left >= right.ptr()->load(std::memory_order_seq_cst);
}

#define _Atomic(T) _Atomic_storage<T>

// Atomic type definitions
typedef _Atomic(bool)         atomic_bool;
typedef _Atomic(char)         atomic_char;
typedef _Atomic(signed char)    atomic_schar;
typedef _Atomic(unsigned char)    atomic_uchar;
typedef _Atomic(short)        atomic_short;
typedef _Atomic(unsigned short)   atomic_ushort;
typedef _Atomic(int)        atomic_int;
typedef _Atomic(unsigned int)     atomic_uint;
typedef _Atomic(long)         atomic_long;
typedef _Atomic(unsigned long)    atomic_ulong;
typedef _Atomic(long long)      atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(char16_t)       atomic_char16_t;
typedef _Atomic(char32_t)       atomic_char32_t;
typedef _Atomic(wchar_t)      atomic_wchar_t;
typedef _Atomic(int_least8_t)     atomic_int_least8_t;
typedef _Atomic(uint_least8_t)    atomic_uint_least8_t;
typedef _Atomic(int_least16_t)    atomic_int_least16_t;
typedef _Atomic(uint_least16_t)   atomic_uint_least16_t;
typedef _Atomic(int_least32_t)    atomic_int_least32_t;
typedef _Atomic(uint_least32_t)   atomic_uint_least32_t;
typedef _Atomic(int_least64_t)    atomic_int_least64_t;
typedef _Atomic(uint_least64_t)   atomic_uint_least64_t;
typedef _Atomic(int_fast8_t)    atomic_int_fast8_t;
typedef _Atomic(uint_fast8_t)     atomic_uint_fast8_t;
typedef _Atomic(int_fast16_t)     atomic_int_fast16_t;
typedef _Atomic(uint_fast16_t)    atomic_uint_fast16_t;
typedef _Atomic(int_fast32_t)     atomic_int_fast32_t;
typedef _Atomic(uint_fast32_t)    atomic_uint_fast32_t;
typedef _Atomic(int_fast64_t)     atomic_int_fast64_t;
typedef _Atomic(uint_fast64_t)    atomic_uint_fast64_t;
typedef _Atomic(intptr_t)       atomic_intptr_t;
typedef _Atomic(uintptr_t)      atomic_uintptr_t;
typedef _Atomic(size_t)       atomic_size_t;
typedef _Atomic(ptrdiff_t)      atomic_ptrdiff_t;
typedef _Atomic(intmax_t)       atomic_intmax_t;
typedef _Atomic(uintmax_t)      atomic_uintmax_t;

// Atomic flag - use std::atomic_flag directly (it's already trivially copyable in most implementations)
typedef std::atomic_flag atomic_flag;
#ifndef ATOMIC_FLAG_INIT
#define ATOMIC_FLAG_INIT {0}
#endif // ATOMIC_FLAG_INIT

// Lock-free macros
#define ATOMIC_BOOL_LOCK_FREE   __GCC_ATOMIC_BOOL_LOCK_FREE
#define ATOMIC_CHAR_LOCK_FREE   __GCC_ATOMIC_CHAR_LOCK_FREE
#define ATOMIC_CHAR16_T_LOCK_FREE __GCC_ATOMIC_CHAR16_T_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE __GCC_ATOMIC_CHAR32_T_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE  __GCC_ATOMIC_WCHAR_T_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE  __GCC_ATOMIC_SHORT_LOCK_FREE
#define ATOMIC_INT_LOCK_FREE    __GCC_ATOMIC_INT_LOCK_FREE
#define ATOMIC_LONG_LOCK_FREE   __GCC_ATOMIC_LONG_LOCK_FREE
#define ATOMIC_LLONG_LOCK_FREE  __GCC_ATOMIC_LLONG_LOCK_FREE
#define ATOMIC_POINTER_LOCK_FREE  __GCC_ATOMIC_POINTER_LOCK_FREE

// Initialization - constructs the atomic in-place
template<typename T>
inline void atomic_init(volatile _Atomic_storage<T>* obj, T value) {
  new (const_cast<_Atomic_storage<T>*>(obj)->data) std::atomic<T>(value);
}

// Lock-free query
template<typename T>
inline bool atomic_is_lock_free(const volatile _Atomic_storage<T>* obj) {
  return obj->ptr()->is_lock_free();
}

// Store operations
template<typename T>
inline void atomic_store(volatile _Atomic_storage<T>* object, T desired) {
  object->ptr()->store(desired);
}

template<typename T>
inline void atomic_store_explicit(volatile _Atomic_storage<T>* object, T desired, 
  memory_order order
) {
  object->ptr()->store(desired, order);
}

// Load operations
template<typename T>
inline T atomic_load(const volatile _Atomic_storage<T>* object) {
  return object->ptr()->load();
}

template<typename T>
inline T atomic_load_explicit(const volatile _Atomic_storage<T>* object, 
  memory_order order
) {
  return object->ptr()->load(order);
}

// Exchange operations
template<typename T>
inline T atomic_exchange(volatile _Atomic_storage<T>* object, T desired) {
  return object->ptr()->exchange(desired);
}

template<typename T>
inline T atomic_exchange_explicit(volatile _Atomic_storage<T>* object, T desired,
  memory_order order
) {
  return object->ptr()->exchange(desired, order);
}

// Compare-exchange operations
template<typename T>
inline bool atomic_compare_exchange_strong(volatile _Atomic_storage<T>* object,
  T* expected, T desired
) {
  return object->ptr()->compare_exchange_strong(*expected, desired);
}

template<typename T>
inline bool atomic_compare_exchange_strong_explicit(volatile _Atomic_storage<T>* object,
  T* expected, T desired,
  memory_order success,
  memory_order failure
) {
  return object->ptr()->compare_exchange_strong(*expected, desired, success, failure);
}

template<typename T>
inline bool atomic_compare_exchange_weak(volatile _Atomic_storage<T>* object,
  T* expected, T desired
) {
  return object->ptr()->compare_exchange_weak(*expected, desired);
}

template<typename T>
inline bool atomic_compare_exchange_weak_explicit(volatile _Atomic_storage<T>* object,
  T* expected, T desired,
  memory_order success,
  memory_order failure
) {
  return object->ptr()->compare_exchange_weak(*expected, desired, success, failure);
}

// Fetch-add operations
template<typename T>
inline T atomic_fetch_add(volatile _Atomic_storage<T>* object, T operand) {
  return object->ptr()->fetch_add(operand);
}

template<typename T>
inline T atomic_fetch_add_explicit(volatile _Atomic_storage<T>* object, T operand,
  memory_order order
) {
  return object->ptr()->fetch_add(operand, order);
}

// Pointer specializations for fetch_add (need ptrdiff_t operand)
template<typename T>
inline T* atomic_fetch_add(volatile _Atomic_storage<T*>* object, ptrdiff_t operand) {
  return object->ptr()->fetch_add(operand);
}

template<typename T>
inline T* atomic_fetch_add_explicit(volatile _Atomic_storage<T*>* object, ptrdiff_t operand,
  memory_order order
) {
  return object->ptr()->fetch_add(operand, order);
}

// Fetch-sub operations
template<typename T>
inline T atomic_fetch_sub(volatile _Atomic_storage<T>* object, T operand) {
  return object->ptr()->fetch_sub(operand);
}

template<typename T>
inline T atomic_fetch_sub_explicit(volatile _Atomic_storage<T>* object, T operand,
  memory_order order
) {
  return object->ptr()->fetch_sub(operand, order);
}

// Pointer specializations for fetch_sub
template<typename T>
inline T* atomic_fetch_sub(volatile _Atomic_storage<T*>* object, ptrdiff_t operand) {
  return object->ptr()->fetch_sub(operand);
}

template<typename T>
inline T* atomic_fetch_sub_explicit(volatile _Atomic_storage<T*>* object, ptrdiff_t operand,
  memory_order order
) {
  return object->ptr()->fetch_sub(operand, order);
}

// Fetch-or operations
template<typename T>
inline T atomic_fetch_or(volatile _Atomic_storage<T>* object, T operand) {
  return object->ptr()->fetch_or(operand);
}

template<typename T>
inline T atomic_fetch_or_explicit(volatile _Atomic_storage<T>* object, T operand,
  memory_order order
) {
  return object->ptr()->fetch_or(operand, order);
}

// Fetch-xor operations
template<typename T>
inline T atomic_fetch_xor(volatile _Atomic_storage<T>* object, T operand) {
  return object->ptr()->fetch_xor(operand);
}

template<typename T>
inline T atomic_fetch_xor_explicit(volatile _Atomic_storage<T>* object, T operand,
  memory_order order
) {
  return object->ptr()->fetch_xor(operand, order);
}

// Fetch-and operations
template<typename T>
inline T atomic_fetch_and(volatile _Atomic_storage<T>* object, T operand) {
  return object->ptr()->fetch_and(operand);
}

template<typename T>
inline T atomic_fetch_and_explicit(volatile _Atomic_storage<T>* object, T operand,
  memory_order order
) {
  return object->ptr()->fetch_and(operand, order);
}

// Fence operations
inline void atomic_thread_fence(memory_order order) {
  std::atomic_thread_fence(order);
}

inline void atomic_signal_fence(memory_order order) {
  std::atomic_signal_fence(order);
}

// Atomic flag operations
inline bool atomic_flag_test_and_set(volatile atomic_flag* object) {
  return object->test_and_set();
}

inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag* object,
  memory_order order
) {
  return object->test_and_set(order);
}

inline void atomic_flag_clear(volatile atomic_flag* object) {
  object->clear();
}

inline void atomic_flag_clear_explicit(volatile atomic_flag* object,
  memory_order order
) {
  object->clear(order);
}

#define memory_order_relaxed std::memory_order_relaxed
#define memory_order_consume std::memory_order_consume
#define memory_order_acquire std::memory_order_acquire
#define memory_order_release std::memory_order_release
#define memory_order_acq_rel std::memory_order_acq_rel
#define memory_order_seq_cst std::memory_order_seq_cst

#define C11_ATOMIC_OK 1

#endif // HAS_CXX_ATOMIC

#if !C11_ATOMIC_OK

#if defined(__GNUC__) || defined(__clang__)

/*
 * Non-atomic implementations of C11 stdatomic.h for backward compatibility
 * with compilers that don't support C11 atomics.
 * 
 * WARNING: These are NOT thread-safe implementations. They provide API
 * compatibility only. For multi-threaded code, you must either:
 *   1. Use a compiler with real atomic support, or
 *   2. Wrap critical sections with your own locking mechanism
 * 
 * For single-threaded embedded systems, these provide the API without
 * the overhead of atomic operations.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory order enumeration (ignored in non-atomic implementation) */
typedef enum {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

/* Atomic flag type */
typedef struct {
    bool value;
} atomic_flag;

#define ATOMIC_FLAG_INIT { false }

/* Atomic types - just wrap the regular types */
typedef struct { bool value; } atomic_bool;
typedef struct { char value; } atomic_char;
typedef struct { signed char value; } atomic_schar;
typedef struct { unsigned char value; } atomic_uchar;
typedef struct { short value; } atomic_short;
typedef struct { unsigned short value; } atomic_ushort;
typedef struct { int value; } atomic_int;
typedef struct { unsigned int value; } atomic_uint;
typedef struct { long value; } atomic_long;
typedef struct { unsigned long value; } atomic_ulong;
typedef struct { long long value; } atomic_llong;
typedef struct { unsigned long long value; } atomic_ullong;
typedef struct { uint_least16_t value; } atomic_uint_least16_t;
typedef struct { int_least16_t value; } atomic_int_least16_t;
typedef struct { uint_least32_t value; } atomic_uint_least32_t;
typedef struct { int_least32_t value; } atomic_int_least32_t;
typedef struct { uint_least64_t value; } atomic_uint_least64_t;
typedef struct { int_least64_t value; } atomic_int_least64_t;
typedef struct { uint_fast16_t value; } atomic_uint_fast16_t;
typedef struct { int_fast16_t value; } atomic_int_fast16_t;
typedef struct { uint_fast32_t value; } atomic_uint_fast32_t;
typedef struct { int_fast32_t value; } atomic_int_fast32_t;
typedef struct { uint_fast64_t value; } atomic_uint_fast64_t;
typedef struct { int_fast64_t value; } atomic_int_fast64_t;
typedef struct { intptr_t value; } atomic_intptr_t;
typedef struct { uintptr_t value; } atomic_uintptr_t;
typedef struct { size_t value; } atomic_size_t;
typedef struct { ptrdiff_t value; } atomic_ptrdiff_t;
typedef struct { intmax_t value; } atomic_intmax_t;
typedef struct { uintmax_t value; } atomic_uintmax_t;

/* Generic atomic type pointer (for compatibility) */
#define _Atomic(T) struct { T value; }

/* Initialization macros */
#define ATOMIC_VAR_INIT(value) { (value) }

/* Atomic flag operations */
static inline bool atomic_flag_test_and_set(volatile atomic_flag *obj) {
    bool old = obj->value;
    obj->value = true;
    return old;
}

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, 
                                                      memory_order order) {
    (void)order;
    return atomic_flag_test_and_set(obj);
}

static inline void atomic_flag_clear(volatile atomic_flag *obj) {
    obj->value = false;
}

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, 
                                               memory_order order) {
    (void)order;
    atomic_flag_clear(obj);
}

/* Generic atomic operations - macros for type-generic interface */
#define atomic_init(obj, value) ((obj)->value = (value))

#define atomic_store(obj, desired) \
    ((obj)->value = (desired))

#define atomic_store_explicit(obj, desired, order) \
    ((void)(order), atomic_store(obj, desired))

#define atomic_load(obj) \
    ((obj)->value)

#define atomic_load_explicit(obj, order) \
    ((void)(order), atomic_load(obj))

#define atomic_exchange(obj, desired) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value = (desired); \
    __old; \
})

#define atomic_exchange_explicit(obj, desired, order) \
    ((void)(order), atomic_exchange(obj, desired))

#define atomic_compare_exchange_strong(obj, expected, desired) ({ \
    bool __success = false; \
    if ((obj)->value == *(expected)) { \
        (obj)->value = (desired); \
        __success = true; \
    } else { \
        *(expected) = (obj)->value; \
    } \
    __success; \
})

#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
    ((void)(succ), (void)(fail), atomic_compare_exchange_strong(obj, expected, desired))

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_strong(obj, expected, desired)

#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
    atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail)

/* Arithmetic operations */
#define atomic_fetch_add(obj, arg) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value += (arg); \
    __old; \
})

#define atomic_fetch_add_explicit(obj, arg, order) \
    ((void)(order), atomic_fetch_add(obj, arg))

#define atomic_fetch_sub(obj, arg) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value -= (arg); \
    __old; \
})

#define atomic_fetch_sub_explicit(obj, arg, order) \
    ((void)(order), atomic_fetch_sub(obj, arg))

#define atomic_fetch_or(obj, arg) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value |= (arg); \
    __old; \
})

#define atomic_fetch_or_explicit(obj, arg, order) \
    ((void)(order), atomic_fetch_or(obj, arg))

#define atomic_fetch_xor(obj, arg) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value ^= (arg); \
    __old; \
})

#define atomic_fetch_xor_explicit(obj, arg, order) \
    ((void)(order), atomic_fetch_xor(obj, arg))

#define atomic_fetch_and(obj, arg) ({ \
    __typeof__((obj)->value) __old = (obj)->value; \
    (obj)->value &= (arg); \
    __old; \
})

#define atomic_fetch_and_explicit(obj, arg, order) \
    ((void)(order), atomic_fetch_and(obj, arg))

/* Fence operations (no-ops in non-atomic implementation) */
static inline void atomic_thread_fence(memory_order order) {
    (void)order;
    /* In a real implementation, this would be a memory barrier */
    /* For embedded single-core systems, might not need anything */
    /* For multi-threaded, you would need compiler barriers at minimum */
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__ ("" ::: "memory");  /* Compiler barrier */
#endif
}

static inline void atomic_signal_fence(memory_order order) {
    (void)order;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__ ("" ::: "memory");  /* Compiler barrier */
#endif
}

/* Lock-free property queries (always false in non-atomic implementation) */
#define atomic_is_lock_free(obj) false

#define ATOMIC_BOOL_LOCK_FREE 0
#define ATOMIC_CHAR_LOCK_FREE 0
#define ATOMIC_CHAR16_T_LOCK_FREE 0
#define ATOMIC_CHAR32_T_LOCK_FREE 0
#define ATOMIC_WCHAR_T_LOCK_FREE 0
#define ATOMIC_SHORT_LOCK_FREE 0
#define ATOMIC_INT_LOCK_FREE 0
#define ATOMIC_LONG_LOCK_FREE 0
#define ATOMIC_LLONG_LOCK_FREE 0
#define ATOMIC_POINTER_LOCK_FREE 0

#ifdef __cplusplus
}
#endif

#warning "Using non-atomic compatibility layer for stdatomic.h - NOT THREAD-SAFE!"

#else // defined(__GNUC__) || defined(__clang__)

#warning "No compatibility with C11 atomics is possible."

#endif // defined(__GNUC__) || defined(__clang__)

#endif // !C11_ATOMIC_OK

#endif // C_ATOMIC_H

