#if !defined(MUDUO_TCPCONNECTION_H)
#define MUDUO_TCPCONNECTION_H

#include <InetAddr.h>
#include <TcpServer.h>  // for declare friend
#include <Callbacks.h>
#include <functional>
#include <memory>
#include <string>
#include <any>

namespace muduo {
    
class EventLoop;        // forward declaration
class Channel;          // forward declaration
class Socket;           // forward declaration
namespace base {
class MemPool;          // forward declartaion
template <typename T>
// using Pdeleter = void(*)(T* ptr);    // failed to construct a unique_ptr which use Pdeleter as 'deleter'
using Pdeleter = std::function<void(T* ptr)>;
} // namespace base 


class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
    friend void TcpServer::HandleNewConnection(int connfd, const InetAddr& remote_addr);
    friend void TcpServer::RemoveConnection(const TcpConnectionPtr& conn);
    /* non-copyable and non-moveable*/
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    /* declare private-domain for factory pattern */
    TcpConnection(EventLoop* owner, base::MemPool* pool, const std::string& name, int sockfd, const InetAddr& local_addr, const InetAddr& remote_addr);

    /// destroy tcp-connection instance and free connection-level memory pool
    static void DestroyTcpConnection(TcpConnection* conn);
    using CloseCallback_t = std::function<void(const TcpConnectionPtr& conn)>;
    enum State { connecting, connected, disconnecting, disconnected };

public:
    /// Factory pattern, Return a Tcp-connection instance
    static TcpConnectionPtr CreateTcpConnection(EventLoop* owner, const std::string& name, int sockfd, const InetAddr& local_addr, const InetAddr& remote_addr);
    ~TcpConnection() noexcept;

    EventLoop* GetEventLoop() { return loop_; }
    const InetAddr& GetLocalAddr() const { return localAddr_; } 
    const InetAddr& GetRemoteAddr() const { return remoteAddr_; } 
    const std::string& GetName() const { return name_; }

    void SetContext(const std::any& ctx) { context_ = ctx; }
    const std::any& GetContext() const { return context_; }
    /// @brief Return mutable context
    std::any& GetContext() { return context_; }
    bool IsConnected() { return state_ == connected; }
    void SetConnectionCallback(const ConnectionCallback_t& cb)
    { connectionCb_ = cb; }
    void SetOnMessageCallback(const MessageCallback_t& cb)
    { onMessageCb_ = cb; }

    /// Thread-safe, can call cross-thread
    void Shutdown();
    
private:
    /// @note Only used by muduo::TcpServer
    void SetOnCloseCallback(const CloseCallback_t& cb)
    { onCloseCb_ = cb; }
    void StepIntoEstablished();
    void StepIntoDestroyed();
    void ShutdownInLoop();

    /* Reactor-handlers */
    void HandleClose();
    void HandleError();
    void HandleRead(const ReceiveTimePoint_t&);
    void HandleWrite();


private:
    EventLoop* loop_;
    base::MemPool* pool_;   /* pool own most of resoucres of the connection */
    std::string name_;
    std::unique_ptr<Socket, base::Pdeleter<Socket>> socket_;
    std::unique_ptr<Channel, base::Pdeleter<Channel>> chan_;
    InetAddr localAddr_;
    InetAddr remoteAddr_;
    std::atomic<State> state_ {connecting};
    std::any context_ {nullptr};  // C++ 17
    ConnectionCallback_t connectionCb_ {nullptr};
    MessageCallback_t onMessageCb_ {nullptr};
    CloseCallback_t onCloseCb_ {nullptr};   // invoke TcpServer::RemoveConnection of Associated TCP-Server
};

} // namespace muduo 
#endif // MUDUO_TCPCONNECTION_H
