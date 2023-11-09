#pragma once

constexpr auto afp_port = 548;

namespace dsi
{
	void dsi_init();
	void dsi_listen(void* data);
	void dsi_shutdown();
	void dsi_attention_thread();
}
