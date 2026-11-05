#pragma once

// Generate a compiler error is an unhandled size is requested.
template <typename T, size_t N>
struct byte_swap_impl;

template <typename T>
struct byte_swap_impl<T, 1>
{
	T operator()(T& swap_it) const { return swap_it; }
};

template <typename T>
struct byte_swap_impl<T, 2>
{
	T operator()(T& swap_it) const { return _byteswap_ushort(swap_it); }
};

template <typename T>
struct byte_swap_impl<T, 4>
{
	T operator()(T& swap_it) const { return _byteswap_ulong(swap_it); }
};

template <typename T>
struct byte_swap_impl<T, 8>
{
	T operator()(T& swap_it) const { return _byteswap_uint64(swap_it); }
};

// Call this function to swap your bytes.
template <typename T>
T byte_swap(T& swap_it) { return byte_swap_impl<T, sizeof(T)>()(swap_it); }
