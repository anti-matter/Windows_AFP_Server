/*
*	ByteSwap.h
*
*	Copyright © Michael J. Conrad. All Rights Reserved.
*/

#pragma once

// Generate a compiler error is an unhandled size is requested.
template <typename T, size_t N>
struct ByteSwapImpl;

template <typename T>
struct ByteSwapImpl<T, 1>
{
	T operator()(T& swapIt) const { return swapIt; }
};

template <typename T>
struct ByteSwapImpl<T, 2>
{
	T operator()(T& swapIt) const { return _byteswap_ushort(swapIt); }
};

template <typename T>
struct ByteSwapImpl<T, 4>
{
	T operator()(T& swapIt) const { return _byteswap_ulong(swapIt); }
};

template <typename T>
struct ByteSwapImpl<T, 8>
{
	T operator()(T& swapIt) const { return _byteswap_uint64(swapIt); }
};

// Call this function to swap your bytes.
template <typename T>
T ByteSwap(T& swapIt) { return ByteSwapImpl<T, sizeof(T)>()(swapIt); }
