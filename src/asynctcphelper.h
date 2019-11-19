#ifndef asynctcphelper_h
#define asynctcphelper_h

extern AsyncServer *asyncTcpServer;

extern std::vector<AsyncClient *> clients;

void AsyncTcpSetup();
extern char bufferAsyncTcp[256];
extern size_t lenBufAsyncTcp;

extern bool bufferAsyncTcpIsUpdated;

#endif