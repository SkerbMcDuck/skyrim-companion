#include "Server.h"

#include <atomic>
#include <cstring>
#include <sstream>
#include <thread>

#ifdef _WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <winsock2.h>
#	include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kBadSocket = INVALID_SOCKET;
static void CloseSocket(socket_t s) { closesocket(s); }
#else
// POSIX build exists only so the server can be tested off-Windows.
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#	include <unistd.h>
using socket_t = int;
static constexpr socket_t kBadSocket = -1;
static void CloseSocket(socket_t s) { ::close(s); }
#endif

namespace companion::server
{
	namespace
	{
		std::atomic<bool> g_running{ false };
		socket_t          g_listen{ kBadSocket };
		std::thread       g_thread;
		Handler           g_handler;

		const char* StatusText(int code)
		{
			switch (code) {
			case 200: return "OK";
			case 400: return "Bad Request";
			case 404: return "Not Found";
			case 405: return "Method Not Allowed";
			default: return "Internal Server Error";
			}
		}

		void SetRecvTimeout(socket_t s, int ms)
		{
#ifdef _WIN32
			DWORD tv = static_cast<DWORD>(ms);
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
			timeval tv{ ms / 1000, (ms % 1000) * 1000 };
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
		}

		void SendAll(socket_t s, const std::string& data)
		{
			std::size_t sent = 0;
			while (sent < data.size()) {
				const int n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
				if (n <= 0) {
					return;
				}
				sent += static_cast<std::size_t>(n);
			}
		}

		void HandleClient(socket_t client)
		{
			SetRecvTimeout(client, 2000);

			// Read until the end of the request headers (GET requests have no body).
			std::string request;
			char        buf[2048];
			while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
				const int n = ::recv(client, buf, sizeof(buf), 0);
				if (n <= 0) {
					break;
				}
				request.append(buf, static_cast<std::size_t>(n));
			}

			Response res;
			std::istringstream line(request.substr(0, request.find("\r\n")));
			std::string        method, target;
			line >> method >> target;

			if (method.empty() || target.empty()) {
				res.status = 400;
				res.body = "bad request";
			} else if (method != "GET") {
				res.status = 405;
				res.body = "only GET is supported";
			} else {
				const auto q = target.find('?');
				const std::string path = q == std::string::npos ? target : target.substr(0, q);
				try {
					res = g_handler(path);
				} catch (...) {
					res = Response{ 500, "text/plain; charset=utf-8", "handler error" };
				}
			}

			std::ostringstream out;
			out << "HTTP/1.1 " << res.status << ' ' << StatusText(res.status) << "\r\n"
				<< "Content-Type: " << res.contentType << "\r\n"
				<< "Content-Length: " << res.body.size() << "\r\n"
				<< "Cache-Control: no-store\r\n"
				<< "Access-Control-Allow-Origin: *\r\n"
				<< "Connection: close\r\n\r\n";
			SendAll(client, out.str());
			SendAll(client, res.body);
			CloseSocket(client);
		}

		void Loop()
		{
			while (g_running) {
				sockaddr_in addr{};
#ifdef _WIN32
				int len = sizeof(addr);
#else
				socklen_t len = sizeof(addr);
#endif
				const socket_t client = ::accept(g_listen, reinterpret_cast<sockaddr*>(&addr), &len);
				if (client == kBadSocket) {
					continue;  // listen socket closed during shutdown, or a transient error
				}
				// Requests are tiny, so handling them one at a time is plenty.
				HandleClient(client);
			}
		}
	}

	bool Start(std::uint16_t port, Handler handler, std::string& error)
	{
		if (g_running) {
			return true;
		}

#ifdef _WIN32
		WSADATA wsa{};
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			error = "WSAStartup failed";
			return false;
		}
#endif

		g_listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (g_listen == kBadSocket) {
			error = "could not create socket";
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only: never reachable from other devices

		if (::bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
			error = "port " + std::to_string(port) + " is already in use";
			CloseSocket(g_listen);
			g_listen = kBadSocket;
			return false;
		}
		if (::listen(g_listen, 16) != 0) {
			error = "listen failed";
			CloseSocket(g_listen);
			g_listen = kBadSocket;
			return false;
		}

		g_handler = std::move(handler);
		g_running = true;
		g_thread = std::thread(Loop);
		g_thread.detach();
		return true;
	}

	void Stop()
	{
		g_running = false;
		if (g_listen != kBadSocket) {
			CloseSocket(g_listen);
			g_listen = kBadSocket;
		}
	}
}
