#pragma once

struct EosQueueItem;

class Eos
{
public:
	bool Init();
	bool Shutdown();

	bool RegisterCallbacks();
	bool UnregisterCallbacks();
	bool BeginSession();
	bool EndSession();
	bool RegisterClient(int id, const char* ip = nullptr);
	bool UnregisterClient(int id);

	bool ReceiveMessageFromClient(int id, const char* data, int size);

protected:
	template <typename F>
	bool QueueWork(F func);

	void ApiThreadProc();
	void TickThreadProc();

private:
	EOS_HPlatform PlatformHandle = 0;
	EOS_HAntiCheatServer AntiCheatHandle = 0;

	volatile bool shutdowned = false;

	std::condition_variable cv;
	std::mutex mutex;
	std::queue<std::shared_ptr<EosQueueItem>> queue;
	std::thread apiThread;
	std::thread tickThread;

	EOS_NotificationId carnid = EOS_INVALID_NOTIFICATIONID;
	EOS_NotificationId mtcnid = EOS_INVALID_NOTIFICATIONID;
};