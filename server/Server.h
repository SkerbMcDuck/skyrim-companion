#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace companion::server
{
	struct Response
	{
		int         status{ 200 };
		std::string contentType{ "text/plain; charset=utf-8" };
		std::string body;
	};

	// Called on the server thread for every GET request. `path` excludes the query string.
	using Handler = std::function<Response(const std::string& path)>;

	// Starts a background thread listening on 127.0.0.1:<port> (this computer only).
	// Returns false if the port could not be opened.
	bool Start(std::uint16_t port, Handler handler, std::string& error);

	void Stop();
}
