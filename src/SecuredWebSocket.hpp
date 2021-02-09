//
// Created by Gegel85 on 06/04/2019.
//

#ifndef CHALLONGESOKU_SECUREDWEBSOCKET_HPP
#define CHALLONGESOKU_SECUREDWEBSOCKET_HPP


#include <random>
#include <SecuredSocket.hpp>

namespace ChallongeSoku
{
#define WEBSOCKET_CODE(code) ((code - 1000 < 0 || code - 1000 > 15) ? ("???") : (codesStrings[code - 1000]))

	class InvalidHandshakeException : public ChallongeAPI::NetworkException {
	public:
		InvalidHandshakeException(const std::string &&str) : NetworkException(std::move(str)) {};
	};

	class InvalidPongException : public ChallongeAPI::NetworkException {
	public:
		InvalidPongException(const std::string &&str) : NetworkException(std::move(str)) {};
	};

	class SecuredWebSocket : public ChallongeAPI::SecuredSocket {
	private:
		std::string _path;
		std::random_device	_rand;
		void	_establishHandshake(const std::string &host);
		void	_pong(const std::string &validator);

	public:
		static const char * const codesStrings[];
		using Socket::connect;

		SecuredWebSocket() = default;
		~SecuredWebSocket() = default;

		const std::string &getPath() const;
		void setPath(const std::string &path);
		void		send(const std::string &value) override;
		void		disconnect() override;
		void		connect(const std::string &host, unsigned short portno) override;
		void		sendHttpRequest(const HttpRequest &request);
		std::string	getAnswer();
		std::string	getRawAnswer();
	};
}


#endif //CHALLONGESOKU_SECUREDWEBSOCKET_HPP
