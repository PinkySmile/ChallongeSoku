//
// Created by Gegel85 on 06/04/2019.
//

#include <cstring>
#include <sstream>
#include <iostream>
#include <Exceptions.hpp>
#include "SecuredWebSocket.hpp"

using namespace ChallongeAPI;

namespace ChallongeSoku
{
	const char * const SecuredWebSocket::codesStrings[] = {
		"Normal Closure",
		"Going Away",
		"Protocol error",
		"Unsupported Data",
		"???",
		"No Status Rcvd",
		"Abnormal Closure",
		"Invalid frame payload data",
		"Policy Violation",
		"Message Too Big",
		"Mandatory Ext",
		"Internal Server Error",
		"???",
		"???",
		"???",
		"TLS handshake",
	};

	void SecuredWebSocket::_establishHandshake(const std::string &host)
	{
		Socket::HttpRequest	request;
		Socket::HttpResponse	response;

		request.host = host;
		request.path = this->_path;
		request.method = "GET";
		request.httpVer = "HTTP/1.1";
		request.header = {
			{"Connection",            "Upgrade"},
			{"Upgrade",               "websocket"},
			{"Sec-WebSocket-Version", "13"},
			{"Sec-WebSocket-Key",     "77CXUYUvC2pMbKIIQZqgiQ=="},
			{"Sec-WebSocket-Protocol","chat, superchat"},
		};
		this->sendHttpRequest(request);
		response = Socket::parseHttpResponse(this->getRawAnswer());
		if (response.returnCode != 101) {
			this->disconnect();
			throw InvalidHandshakeException("WebSocket Handshake failed: Server answered with code " + std::to_string(response.returnCode) + " but 101 was expected");
		}

	}

	void SecuredWebSocket::send(const std::string &value)
	{
		std::stringstream stream;
		std::string	result = value;
		unsigned	random_value = this->_rand();
		std::string	key = std::string("") +
			static_cast<char>((random_value >> 24U) & 0xFFU) +
			static_cast<char>((random_value >> 16U) & 0xFFU) +
			static_cast<char>((random_value >> 8U) & 0xFFU) +
			static_cast<char>(random_value & 0xFFU);

		for (unsigned i = 0; i < result.size(); i++)
			result[i] = result[i] ^ key[i % 4];
		stream << static_cast<char>(0x81);
		stream << static_cast<char>(0x80 + (value.size() <= 125 ? value.size() : (126 + (value.size() > 65535))));
		if (value.size() > 65535) {
			stream << static_cast<char>(value.size() >> 24U);
			stream << static_cast<char>(value.size() >> 16U);
			stream << static_cast<char>(value.size() >> 8U);
			stream << static_cast<char>(value.size());
		} else if (value.size() > 125) {
			stream << static_cast<char>(value.size() >> 8U);
			stream << static_cast<char>(value.size());
		}
		stream << key << result;
		SecuredSocket::send(stream.str());
	}

	void SecuredWebSocket::_pong(const std::string &validator)
	{
		std::stringstream stream;
		unsigned char byte = 0x8A;

		if (validator.size() > 125)
			throw InvalidPongException("Pong validator cannot be longer than 125B");
		stream << byte;
		byte = 0x80 + validator.size();
		stream << byte;
		SecuredSocket::send(stream.str());
	}

	std::string SecuredWebSocket::getAnswer()
	{
		std::string	result;
		std::string	key;
		unsigned long	length;
		unsigned char	opcode;
		bool	isMasked;
		bool	isEnd;

		if (!this->isOpen())
			throw NotConnectedException("This socket is not connected to a server");

		opcode = this->read(1)[0];
		isEnd = (opcode >> 7U);
		opcode &= 0xFU;

		length = this->read(1)[0];
		isMasked = (length >> 7U);
		length &= 0x7FU;

		if (length == 126)
			length = (static_cast<unsigned char>(this->read(1)[0]) << 8U) +
				static_cast<unsigned char>(this->read(1)[0]);
		else if (length == 127)
			length = (static_cast<unsigned char>(this->read(1)[0]) << 24U) +
				(static_cast<unsigned char>(this->read(1)[0]) << 16U) +
				(static_cast<unsigned char>(this->read(1)[0]) << 8U) +
				static_cast<unsigned char>(this->read(1)[0]);

		if (isMasked)
			key = this->read(4);

		if (opcode == 0x9) {
			this->_pong(result);
			return this->getAnswer();
		}

		result = this->read(length);
		if (isMasked) {
			for (unsigned i = 0; i < result.size(); i++)
				result[i] ^= key[i % 4];
		}

		if (opcode == 0x8) {
			this->disconnect();
			int code = (static_cast<unsigned char>(result[0]) << 8U) + static_cast<unsigned char>(result[1]);
			throw ConnectionTerminatedException("Server closed connection with code " + std::to_string(code) + " (" + WEBSOCKET_CODE(code) + ")", code);
		}

		if (!isEnd)
			return result + this->getAnswer();
		return result;
	}

	void SecuredWebSocket::disconnect()
	{
		std::stringstream stream;
		std::string	result = "\x03\xe8";
		unsigned	random_value = this->_rand();
		std::string	key = std::string() +
			static_cast<char>((random_value >> 24U) & 0xFFU) +
			static_cast<char>((random_value >> 16U) & 0xFFU) +
			static_cast<char>((random_value >> 8U) & 0xFFU) +
			static_cast<char>(random_value & 0xFFU);

		for (unsigned i = 0; i < result.size(); i++)
			result[i] = result[i] ^ key[i % 4];
		stream << "\x88\x82" << key << result;
		SecuredSocket::send(stream.str());
		SecuredSocket::disconnect();
	}

	std::string SecuredWebSocket::getRawAnswer()
	{
		return this->readUntilEOF();
	}

	void SecuredWebSocket::sendHttpRequest(const Socket::HttpRequest &request)
	{
		std::string requestString = generateHttpRequest(request);

		SecuredSocket::send(requestString);
	}

	void SecuredWebSocket::connect(const std::string &host, unsigned short portno)
	{
		SecuredSocket::connect(host, portno);

		std::string realHost = host;

		if (portno != 443)
			realHost += ":" + std::to_string(portno);
		this->_establishHandshake(realHost);
	}

	const std::string &SecuredWebSocket::getPath() const
	{
		return this->_path;
	}

	void SecuredWebSocket::setPath(const std::string &path)
	{
		this->_path = path;
	}
}
