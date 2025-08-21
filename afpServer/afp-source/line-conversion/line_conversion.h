#pragma once
#include "afp_net_buffer.h"
#include "afp_registry.h"

constexpr auto line_conversion_default_extensions = L".p;.c;.cp;.cpp;.h;.hpp;.txt";

enum line_ending : CHAR
{
	line_conversion_cr = '\r',
	line_conversion_lf = '\n'
};

class line_conversion
{
public:
	line_conversion();

	[[nodiscard]] bool should_convert(const std::wstring& extension) const;
	[[nodiscard]] regerror get_status() const { return m_status_; }
	[[nodiscard]] const std::vector<std::wstring>& get_line_convert_extensions() const
	{
		return m_line_convert_extensions_;
	}

	void convert_buffer(const afp_net_buffer& buffer, const DWORD* length, const line_ending convert_to) const;

private:
	regerror m_status_;
	std::vector<std::wstring> m_line_convert_extensions_;
};

