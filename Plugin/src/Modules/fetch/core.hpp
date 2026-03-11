/**
 * @file core.hpp
 * @brief Core structures and registry for fetch module
 * @license GPL v2.0 License
 *
 * Contains the main data structures (FetchRequest, FetchResponse, FetchContext)
 * and the thread-safe registry for managing async requests.
 */

#pragma once

#include <Windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <lua.hpp>




// Forward declarations
struct Rain;
struct lua_State;




namespace core {

	/**
	 * @struct FetchResponse
	 * @brief HTTP response data container
	 */
	struct FetchResponse {
		int status = 0;
		std::vector<BYTE> body;
		std::map<std::string, std::string> headers;
		std::map<std::string, std::string> cookies;
		std::string error;
		std::string text;

		static const int STATUS_NETWORK_ERROR = -1; // Erro genérico de rede
		static const int STATUS_CANCELLED = -2; // Requisição cancelada pelo usuário
		static const int STATUS_INVALID_URL = -3; // URL inválida/mal formatada
		static const int STATUS_NO_INTERNET = -4; // Sem conexão com internet
		static const int STATUS_THREAD_ERROR = -5; // Erro ao criar thread

		static const int STATUS_ABORTED = -6; // Operação abortada (E_ABORT)
		static const int STATUS_CONNECTION_LOST = -7; // Conexão perdida durante transferência
		static const int STATUS_SSL_ERROR = -8; // Erro de SSL/TLS
		static const int STATUS_PROXY_ERROR = -9; // Erro de proxy
		static const int STATUS_TIMEOUT_DNS = -10; // Timeout na resolução DNS
		static const int STATUS_TIMEOUT_CONNECT = -11; // Timeout na conexão
		static const int STATUS_TIMEOUT_SEND = -12; // Timeout no envio
		static const int STATUS_TIMEOUT_RECEIVE = -13; // Timeout no recebimento
		static const int STATUS_CHUNKED_ERROR = -14; // Erro ao processar chunked encoding

		FetchResponse() = default;
	};



	/**
	 * @struct FetchRequest
	 * @brief HTTP request configuration
	 */
	struct FetchRequest {
		std::wstring url;
		std::wstring method = L"GET";
		std::map<std::wstring, std::wstring> headers;
		std::vector<BYTE> body;

		// Timeout geral (para compatibilidade)
		int timeout = 30000;

		// Timeouts específicos por fase (em milissegundos)
		int dnsTimeout = 0; // 0 = usa timeout geral
		int connectTimeout = 0; // 0 = usa timeout geral
		int sendTimeout = 0; // 0 = usa timeout geral
		int receiveTimeout = 0; // 0 = usa timeout geral

		bool followRedirects = true;
		bool HTTPVersion = false;

		FetchRequest() = default;
	};



	/**
	 * @struct FetchContext
	 * @brief Async fetch context with thread-safe state
	 */
	struct FetchContext {
		// Configuration
		FetchRequest request;

		// Thread-safe state
		std::atomic<bool> threadActive{ false };
		std::atomic<bool> completed{ false };
		std::atomic<bool> cancelled{ false };
		std::mutex mutex;

		// Results
		FetchResponse response;

		// Lua integration
		Rain *rain = nullptr;
		int refSelf = -1;
		int refSelfLua = LUA_NOREF;
		int refCallback = LUA_NOREF;

		FetchContext( Rain *r );
		~FetchContext();

		void releaseLuaRefs();
	};



	/**
	 * @class ContextRegistry
	 * @brief Global thread-safe registry for fetch contexts
	 */
	class ContextRegistry {
	public:
		static ContextRegistry &instance();

		// No copying
		ContextRegistry( const ContextRegistry & ) = delete;
		ContextRegistry &operator=( const ContextRegistry & ) = delete;

		int registerContext( std::shared_ptr<FetchContext> ctx );
		std::shared_ptr<FetchContext> getContext( int id );
		void removeContext( int id );
		void removeAllByRain( Rain *rain );
		size_t count();

	private:
		ContextRegistry() = default;
		~ContextRegistry() = default;

		std::mutex mutex_;
		std::unordered_map<int, std::shared_ptr<FetchContext>> contexts_;
		std::atomic<int> nextId_{ 1 };
	};



	// Public API functions
	void CleanupContexts( Rain *rain );

} // namespace core
