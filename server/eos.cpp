#include "stdafx.h"
#include "eos.h"

/****************** replace with your ids ***********************/

#define GAMENAME "leak-server"
#define PRODUCTID "xxx"
#define SANDBOXID "xxx"
#define DEPLOYMENTID "xxx"
#define CLIENTID "xxx"
#define CLIENTSECRET "xxx"

/****************** replace with your ids ***********************/

struct EosQueueItem
{
	std::function<bool()> func;
	std::promise<bool> ret;
};

template <typename F>
bool Eos::QueueWork(F func)
{
	auto pItem = new EosQueueItem();
	pItem->func = func;
	std::future<bool> ret = pItem->ret.get_future();

	mutex.lock();
	queue.push(std::shared_ptr<EosQueueItem>(pItem));
	mutex.unlock();
	cv.notify_one();

	ret.wait();
	return ret.get();
}

bool Eos::Init()
{
	apiThread = std::thread(std::bind(&Eos::ApiThreadProc, this));

	return QueueWork([this]() {
			// Init EOS SDK
			EOS_InitializeOptions SDKOptions = {};
			SDKOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;
			SDKOptions.AllocateMemoryFunction = nullptr;
			SDKOptions.ReallocateMemoryFunction = nullptr;
			SDKOptions.ReleaseMemoryFunction = nullptr;
			SDKOptions.ProductName = GAMENAME;
			SDKOptions.ProductVersion = "1.0";
			SDKOptions.Reserved = nullptr;
			SDKOptions.SystemInitializeOptions = nullptr;
			SDKOptions.OverrideThreadAffinity = nullptr;

			EOS_EResult InitResult = EOS_Initialize(&SDKOptions);
			if (InitResult != EOS_EResult::EOS_Success)
			{
				printf("[EOS] Init failed: %d\n", (int)InitResult);
				return false;
			}

			printf("[EOS] Initialized. Setting Logging Callback ...\n");

			EOS_EResult SetLogCallbackResult = EOS_Logging_SetCallback([](const EOS_LogMessage* InMsg) {
				if (InMsg != nullptr && InMsg->Level != EOS_ELogLevel::EOS_LOG_Off)
				{
					if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Error || InMsg->Level == EOS_ELogLevel::EOS_LOG_Fatal)
					{
						printf("[EOS] ERROR %s: %s\n", InMsg->Category, InMsg->Message);
					}
					else if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Warning)
					{
						printf("[EOS] WARNING %s: %s\n", InMsg->Category, InMsg->Message);
					}
					else
					{
						printf("[EOS] %s: %s\n", InMsg->Category, InMsg->Message);
					}
					fflush(stdout);
				}
				});

			if (SetLogCallbackResult != EOS_EResult::EOS_Success)
			{
				printf("[EOS] Set Logging Callback Failed: %d\n", (int)SetLogCallbackResult);
			}
			else
			{
				printf("[EOS] Logging Callback Set\n");
				EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Verbose);
			}

			// Create platform instance
			EOS_Platform_Options PlatformOptions = {};
			PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
			PlatformOptions.bIsServer = EOS_TRUE;
			PlatformOptions.EncryptionKey = nullptr;
			PlatformOptions.OverrideCountryCode = nullptr;
			PlatformOptions.OverrideLocaleCode = nullptr;
			PlatformOptions.Flags = EOS_PF_DISABLE_OVERLAY; // no overlay needed for the server app
			PlatformOptions.CacheDirectory = nullptr;
			PlatformOptions.ProductId = PRODUCTID;
			PlatformOptions.SandboxId = SANDBOXID;
			PlatformOptions.DeploymentId = DEPLOYMENTID;
			PlatformOptions.ClientCredentials.ClientId = CLIENTID;
			PlatformOptions.ClientCredentials.ClientSecret = CLIENTSECRET;
			PlatformOptions.RTCOptions = nullptr;

			PlatformOptions.Reserved = NULL;

			PlatformHandle = EOS_Platform_Create(&PlatformOptions);
			if (PlatformHandle == nullptr)
			{
				printf("[EOS] Unable to create platform handle!\n");
				return false;
			}

			AntiCheatHandle = EOS_Platform_GetAntiCheatServerInterface(PlatformHandle);

		tickThread = std::thread(std::bind(&Eos::TickThreadProc, this));

		return true;
		});
}

bool Eos::Shutdown()
{
	bool ret = QueueWork([this]() {
		EOS_EResult ShutdownResult = EOS_Shutdown();
		if (ShutdownResult != EOS_EResult::EOS_Success)
		{
			printf("[EOS] shutdown failed: %d\n", (int)ShutdownResult);
			return false;
		}
		return true;
		});

	QueueWork([this]() { shutdowned = true; return true; });
	if (apiThread.joinable())
		apiThread.join();
	if (tickThread.joinable())
		tickThread.join();

	return ret;
}

void __stdcall carCallback(const EOS_AntiCheatCommon_OnClientActionRequiredCallbackInfo* Data)
{
	int id = (int)Data->ClientHandle;

	switch (Data->ClientAction)
	{
	case EOS_EAntiCheatCommonClientAction::EOS_ACCCA_RemovePlayer:
		printf("kick client %d (%s).\n", id, Data->ActionReasonDetailsString);
		break;
	default:
		printf("client %d action required callback: %d, %s\n", id, (int)Data->ClientAction, Data->ActionReasonDetailsString);
		break;
	}
}

void __stdcall mtcCallback(const EOS_AntiCheatCommon_OnMessageToClientCallbackInfo* Data)
{
	int id = (int)Data->ClientHandle;
	printf("client %d message to client callback.\n", id);
}

bool Eos::RegisterCallbacks()
{
	return QueueWork([this]() {

		{
			EOS_AntiCheatServer_AddNotifyClientActionRequiredOptions options;
			options.ApiVersion = EOS_ANTICHEATSERVER_ADDNOTIFYCLIENTAUTHSTATUSCHANGED_API_LATEST;

			carnid = EOS_AntiCheatServer_AddNotifyClientActionRequired(AntiCheatHandle, &options, nullptr, &carCallback);
			if (carnid == EOS_INVALID_NOTIFICATIONID)
				return false;
		}

		{
			EOS_AntiCheatServer_AddNotifyMessageToClientOptions options;
			options.ApiVersion = EOS_ANTICHEATSERVER_ADDNOTIFYMESSAGETOCLIENT_API_LATEST;

			mtcnid = EOS_AntiCheatServer_AddNotifyMessageToClient(AntiCheatHandle, &options, nullptr, &mtcCallback);
			if (mtcnid == EOS_INVALID_NOTIFICATIONID)
				return false;
		}

		return true;
		});
}

bool Eos::UnregisterCallbacks()
{
	return QueueWork([this]() {
		if (carnid != EOS_INVALID_NOTIFICATIONID)
		{
			EOS_AntiCheatServer_RemoveNotifyClientActionRequired(AntiCheatHandle, carnid);
			carnid = EOS_INVALID_NOTIFICATIONID;
		}

		if (mtcnid != EOS_INVALID_NOTIFICATIONID)
		{
			EOS_AntiCheatServer_RemoveNotifyMessageToClient(AntiCheatHandle, mtcnid);
			mtcnid = EOS_INVALID_NOTIFICATIONID;
		}

		return true;
		});
}

bool Eos::BeginSession()
{
	return QueueWork([this]() {
		EOS_AntiCheatServer_BeginSessionOptions options;
		options.ApiVersion = EOS_ANTICHEATSERVER_BEGINSESSION_API_LATEST;
		options.RegisterTimeoutSeconds = 10;
		options.ServerName = "server";
		options.bEnableGameplayData = false;
		options.LocalUserId = nullptr;

		return EOS_AntiCheatServer_BeginSession(AntiCheatHandle, &options) == EOS_EResult::EOS_Success;
		});
}

bool Eos::EndSession()
{
	return QueueWork([this]() {
		EOS_AntiCheatServer_EndSessionOptions options;
		options.ApiVersion = EOS_ANTICHEATSERVER_ENDSESSION_API_LATEST;

		return EOS_AntiCheatServer_EndSession(AntiCheatHandle, &options) == EOS_EResult::EOS_Success;
		});
}

bool Eos::RegisterClient(int id, const char* ip)
{
	return QueueWork([this, id, ip]() {
		std::string accountId = std::to_string(id);

		EOS_AntiCheatServer_RegisterClientOptions options;
		options.ApiVersion = EOS_ANTICHEATSERVER_REGISTERCLIENT_API_LATEST;
		options.ClientHandle = (void*) id;
		options.ClientType = EOS_EAntiCheatCommonClientType::EOS_ACCCT_ProtectedClient;
		options.ClientPlatform = EOS_EAntiCheatCommonClientPlatform::EOS_ACCCP_Windows;
		options.AccountId = accountId.c_str();
		options.IpAddress = ip;

		return EOS_AntiCheatServer_RegisterClient(AntiCheatHandle, &options) == EOS_EResult::EOS_Success;
		});
}

bool Eos::UnregisterClient(int id)
{
	return QueueWork([this, id]() {
		EOS_AntiCheatServer_UnregisterClientOptions options;
		options.ApiVersion = EOS_ANTICHEATSERVER_UNREGISTERCLIENT_API_LATEST;
		options.ClientHandle = (void*) id;

		return EOS_AntiCheatServer_UnregisterClient(AntiCheatHandle, &options) == EOS_EResult::EOS_Success;
		});
}

bool Eos::ReceiveMessageFromClient(int id, const char* data, int size)
{
	return QueueWork([this, id, data, size]() {
		EOS_AntiCheatServer_ReceiveMessageFromClientOptions options;
		options.ApiVersion = EOS_ANTICHEATSERVER_RECEIVEMESSAGEFROMCLIENT_API_LATEST;
		options.ClientHandle = (void*) id;
		options.Data = data;
		options.DataLengthBytes = size;

		return EOS_AntiCheatServer_ReceiveMessageFromClient(AntiCheatHandle, &options) == EOS_EResult::EOS_Success;
		});
}

void Eos::ApiThreadProc()
{
	std::unique_lock<std::mutex> lock(mutex);
	while (!shutdowned && !queue.empty())
	{
		while (!queue.empty())
		{
			auto item = queue.front();
			queue.pop();
			item->ret.set_value(item->func());
		}

		if (shutdowned)
			break;

		cv.wait(lock);
	}
	printf("Eos::ThreadProc() done.\n");
}

void Eos::TickThreadProc()
{
	while (!shutdowned)
	{
		EOS_Platform_Tick(PlatformHandle);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
