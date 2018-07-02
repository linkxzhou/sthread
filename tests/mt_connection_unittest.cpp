#include "gtest/googletest/include/gtest/gtest.h"
#include "../include/mt_connection.h"

MTHREAD_NAMESPACE_USING

TEST(ConnectionTest, UdpShort)
{
	UdpClientConnection *conn = new UdpClientConnection();

	Eventer* ev = new Eventer();

	conn->SetEventer(ev);

	IMtMsgBuffer* msg_buff = GetInstance<IMsgBufferPool>() -> GetMsgBuffer(1024);

	conn->SetIMtMsgBuffer(msg_buff);

	conn->SetIMtActon((IMtActionBase *)0x0001);

	GetInstance<EventProxyer>() -> Init(10000);

	int fd = conn->CreateSocket();

	LOG_TRACE("fd : %d", fd);

	GetInstance<EventProxyer>() -> SetTimeout(5000);
	GetInstance<EventProxyer>() -> Dispatch();

	conn->SendData();

	GetInstance<EventProxyer>() -> SetTimeout(5000);
	GetInstance<EventProxyer>() -> Dispatch();

	safe_delete(conn);
}

// 测试所有的功能
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}