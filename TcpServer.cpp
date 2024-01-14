#include <muduo/EventLoopThreadPool.h>
#include <muduo/base/SocketOps.h>
#include <muduo/TcpConnection.h>
#include <muduo/TcpServer.h>
#include <muduo/EventLoop.h>
#include <muduo/Acceptor.h>
#include <muduo/InetAddr.h>

using namespace muduo;

TcpServer::TcpServer(EventLoop* loop, const InetAddr& addr, const std::string& name)
    : loop_(loop)
    , name_(name)
    , addr_(addr)
#ifdef MUDUO_USE_MEMPOOL
    , acceptor_(new (loop_->GetMemoryPool().get()) Acceptor(loop_, addr_, true),
            std::bind(&base::DestroyWithMemPool<Acceptor>, std::placeholders::_1, loop_->GetMemoryPool().get()))   // FIXME: set "option reuse-port" by evnironment-variable  
    , ioThreadPool_(new (loop_->GetMemoryPool().get()) EventLoopThreadPool(loop_, name_),
            std::bind(&base::DestroyWithMemPool<EventLoopThreadPool>, std::placeholders::_1, loop_->GetMemoryPool().get()))
    , conns_(loop_->GetMemoryPool())
#else
    , acceptor_(std::make_unique<Acceptor>(loop_, addr_, true))   // FIXME: set "option reuse-port" by evnironment-variable  
    , ioThreadPool_(std::make_unique<EventLoopThreadPool>(loop, name_))
    , conns_()
#endif
{
    acceptor_->SetNewConnectionCallback(std::bind(&TcpServer::HandleNewConnection, this,
        std::placeholders::_1, std::placeholders::_2));    
}

TcpServer::~TcpServer() noexcept {
    loop_->AssertInLoopThread();
    LOG_TRACE << "TcpServer[" << this << "] is destructing";
    for (auto& item : conns_) {
        TcpConnectionPtr cur_conn(item.second);
        item.second.reset();
        cur_conn->GetEventLoop()->RunInEventLoop(std::bind(&TcpConnection::StepIntoDestroyed, cur_conn));
    }
}

std::string TcpServer::GetIp() const {
    return acceptor_->GetIp();
}

std::string TcpServer::GetIpPort() const {
    return acceptor_->GetIpPort();
}

void TcpServer::HandleNewConnection(int connfd, const InetAddr& remote_addr) {
    loop_->AssertInLoopThread();
    std::string new_conn_name = name_ + remote_addr.GetIpPort() + "@" + std::to_string(nextConnID_++); 
    InetAddr local_addr(sockets::getLocalAddr(connfd));
    EventLoop* cur_loop = ioThreadPool_->GetNextLoop();

    TcpConnectionPtr new_conn_ptr;
#ifdef MUDUO_USE_MEMPOOL
    // The TcpConnection instance be allocated from memory pool
    assert(loop_->GetMemoryPool());
    new_conn_ptr = std::allocate_shared<TcpConnection, base::allocator<TcpConnection>>(
        base::allocator<TcpConnection>(loop_->GetMemoryPool()),
        cur_loop, new_conn_name, connfd, local_addr, remote_addr
    );
#else
    // The TcpConnection instance be allocated from heap
    new_conn_ptr = std::make_shared<TcpConnection>(cur_loop, new_conn_name, connfd, local_addr, remote_addr);
#endif

    LOG_INFO << "TcpServer::HandleNewConnection: new connection [" << new_conn_name << "] from " << remote_addr.GetIpPort();
    conns_[new_conn_name] = new_conn_ptr;   // add current connection to list
    new_conn_ptr->SetConnectionCallback(connectionCb_);
    new_conn_ptr->SetOnMessageCallback(messageCb_);
    new_conn_ptr->SetOnCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
    new_conn_ptr->SetWriteCompleteCallback(writeCompleteCb_);
    
    cur_loop->RunInEventLoop(std::bind(&TcpConnection::StepIntoEstablished, new_conn_ptr));
}

void TcpServer::RemoveConnection(const TcpConnectionPtr& conn) {
    loop_->RunInEventLoop(std::bind(&TcpServer::RemoveConnectionInLoop, this, conn));
}

void TcpServer::RemoveConnectionInLoop(const TcpConnectionPtr& conn) {
    loop_->AssertInLoopThread();
    assert(conn.use_count() > 1);
    int ret = conns_.erase(conn->GetName());
    assert(ret == 1); (void)ret;
    conn->GetEventLoop()->RunInEventLoop(std::bind(&TcpConnection::StepIntoDestroyed, conn));
}

void TcpServer::ListenAndServe() {
    bool expected = false;
    if (serving_.compare_exchange_strong(expected, true)) { // CAS
        // start io-threads   
        loop_->RunInEventLoop(std::bind(&EventLoopThreadPool::BuildAndRun, ioThreadPool_.get()));

        // start listenting
        loop_->RunInEventLoop(std::bind(&Acceptor::Listen, acceptor_.get()));
    }
}

void TcpServer::SetIoThreadNum(int n) {
    assert(n >= 0);
    ioThreadPool_->SetPoolSize(n);
}

void TcpServer::SetIothreadInitCallback(const IoThreadInitCallback_t& cb) {
    ioThreadPool_->SetThreadInitCallback(cb);
}
