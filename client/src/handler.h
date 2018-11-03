int handleHelp();

int handleUser(char *user, int connSock);

int handlePassword(char *password, int connSock);

int handleTransferedBytes(int connSock);

int handleConcurrentConection(int connSock);

int handleActiveTransformation(int connSock);

int handleBufferSize(int connSock);

int handleTransformationChange(int connSock);

int handleTimeOut(int connSock);

int handleQuit(int connSock);

int handleHistoricAccess(int connSock);

void printDatagram();