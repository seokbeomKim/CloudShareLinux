/*
 * operator.c
 *
 *  Created on: Sep 6, 2015
 *      Author: Sukbeom Kim (chaoxifer@gmail.com)
 */

#include "operator.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "error_code.h"
#include <signal.h>
/*
 * operator thread
 * 프로그램이 실행되면 operator 쓰레드가 실행된다. C 프로그램과
 * Java 기반의 서버 간의 통신을 위해서 Java 프로그램의 메세지를
 * 항상 listening 하고 있다.
 */

// Java Server와 소켓 통신하기 위해 주고받는 프로토콜
// protocol.h 에 정의
#include "protocol.h"
#define _DEBUG_

extern bool isOperatorRunning;

MESSAGE message;
void	receiveAndHandleMessage(void);
bool	send_message(MESSAGE* msg);
void	synchronizeMetaFiles(void);
bool	checkConnection(void);

// 소켓 통신에 사용되는 버퍼
char buffer[BUFFER_SIZE];

// IPC Message 핸들러
void	check_connection(char *msg);
void	refresh(char *msg);
void	filelist(char *msg);

typedef struct {
	void (*check_connection)(char* msg);
	void (*refresh)(char* msg);
	void (*filelist)(char* msg);
} HANDLER;
HANDLER handlers;

int sockfd_in;
int sockfd_out;
struct sockaddr_in serv_addr;
struct hostent *server;

void initHandlers() {
	handlers.check_connection = &check_connection;
	handlers.refresh = &refresh;
	handlers.filelist = &filelist;
}

void initSockets() {
	sockfd_in	= socket(AF_INET, SOCK_STREAM, 0);
	sockfd_out	= socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd_in < 0 || sockfd_out < 0)
	{
		perror("ERROR opening socket");
		_exit(FAILED_OPEN_SOCKET);
	}

	// 소켓 연결 대기
	server	= gethostbyname("127.0.0.1");
	if (server == NULL)
	{
		perror("ERROR, host not found\n");
		_exit(HOST_NOT_FOUND);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT_NUM_IN);
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	if (connect(sockfd_in,(struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR connecting to server(in-socket)");
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT_NUM_OUT);
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
	if (connect(sockfd_out,(struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR connecting to server(out-socket)");
	}

	// 연결 확인 메세지
	MESSAGE msg = { REQUEST_CHECK_CONNECTION, "" };
	if (send_message(&msg) == false) {
		fprintf(stderr, "Failed to send PING to Operator. There might be connection problem.\n");
		_exit(FAILED_SEND_MSG);
	}
}

/*
 * checkConnection
 * Operator와의 연결을 확인한다.
 *
 */
bool checkConnection(void)
{
	// 연결 확인 메세지
	int error = 0;
	socklen_t len = sizeof (error);
	int retval = getsockopt (sockfd_out, SOL_SOCKET, SO_ERROR, &error, &len);

	if (retval != 0) {
	    /* there was a problem getting the error code */
	    fprintf(stdout, "error getting socket error code: %s\n", strerror(retval));
	    return false;
	}

	if (error != 0) {
	    /* socket has a non zero error status */
	    fprintf(stdout, "socket error: %s\n", strerror(error));
	    return false;
	}
	return true;
}

void* listening(void* arg)
{
	isOperatorRunning = true;

	// 메세지 핸들러 초기화
	initHandlers();

	// 소켓 연결 준비
	initSockets();

	/*
	 * 프로그램 시작 전, 파일의 리스트를 초기화하여야 하기 때문에 이 부분을 여기서 처리한다.
	 * 파일 리스트의 동기화는 메타데이터파일(.CloudShare 디렉토리 내)의 동기화로 이루어지므로,
	 * 일차적으로 .CloudShare 디렉토리 내의 파일 리스트를 통해 readdir을 로드하도록 한다.
	 * 여기서 실행되는 초기화 명령을 통해 thread에서 메타파일을 비동기적으로 다운로드 하도록 한다.
	 */
	synchronizeMetaFiles();

	// 연결이 성공하면 루프 돌면서 External Service와 통신한다.
	while (true) {
		// ExternalService로부터 받은 ACK 또한 Operation을 수행
		receiveAndHandleMessage();
		sleep(LATENCY);
	}

	return (void *)0;
}

/*
 * receiveAndHandleMessage
 * 메세지를 받아서 처리하는 함수이다. 메세지를 read하면 분석하여 메세지의 타입을 결정하고
 * 타입에 따라 적절한 핸들러를 호출한다.
 * 수신되는 메세지의 형태는 다음과 같다.
 * i.e.
 * 		request_disk_update::a.txt,b.txt,c.txt,d.txt
 */
void receiveAndHandleMessage(void)
{
	if (read(sockfd_in, buffer, BUFFER_SIZE) < 0) {
		fprintf(stderr, "Failed to receive message");
	}

	// 타입에 적절한 핸들러를 호출한다.
	if (strncmp(buffer, ACK_CHECK_CONNECTION, strlen(ACK_CHECK_CONNECTION)) == 0) {
		handlers.check_connection(buffer);
	}
	else if (strncmp(buffer, ACK_FILELIST, strlen(ACK_FILELIST)) == 0) {
		handlers.filelist(buffer);
	}
	else if (strncmp(buffer, ACK_REFRESH, strlen(ACK_REFRESH)) == 0) {
		handlers.refresh(buffer);
	}

	bzero(buffer, BUFFER_SIZE);
}

// ACK 핸들러
void check_connection(char* msg)
{
	fprintf(stdout, "ACK from Operator - Check Connection");
}

void refresh(char *msg)
{
	fprintf(stdout, "ACK from Operator - Refresh\n");
}

void filelist(char *msg)
{
	fprintf(stdout, "ACK from Operator - FileList\n");
}

/*
 * ============
 * FUSE 관련 함수
 * ============
 * FUSE 관련 함수에서 호출되는 함수들을 정의한다. 사용자가 요청한 작업은 해당 프로그램에서 수행하지 않고
 * 자바 프로그램으로 메세지를 보낸 후 수행한다.
 */
/*
 * synchronizeMetaFiles
 * 연결되어 있는 클라이언트들에게 파일리스트를 요청하여 현재 로컬에 없는 파일 메타데이터를 다운로드하여
 * 파일리스트를 동기화한다.
 */
void synchronizeMetaFiles(void)
{
	MESSAGE msg = {
			REQUEST_FILELIST,
			""
	};
	if (send_message(&msg) == false) {
		fprintf(stderr, "Failed to send PING to Operator. There might be connection problem.\n");
		_exit(FAILED_SEND_MSG);
	}
}
/*
 * requestFileDownload
 * 파일 다운로드를 요청한다.
 */
// read의 경우는 연달아서 발생하는 경우가 있기 때문에 이를 처리하기 위해 prev_msg를 이용한다.
MESSAGE prev_msg;
void requestFileDownload(char* filepath)
{
	fprintf(stdout, "Request file download...\n");
	MESSAGE msg;
	msg.type = REQUEST_DOWNLOAD;
	strcpy(msg.value, filepath);

	// prev_msg와 비교 후, 같은 경우에는 download 요청을 하지 않는다.
	if (prev_msg.type == msg.type &&
			strcmp(prev_msg.value, msg.value) == 0) {
		fprintf(stdout, "Skip send message..\n");
		return;
	}

	prev_msg.type = msg.type;
	strcpy(prev_msg.value, msg.value);
	if (send_message(&msg) == false) {
		fprintf(stderr, "Failed to send PING to Operator. There might be connection problem.\n");
		_exit(2);
	}
}

/*
 * requestFileUpload
 * 파일 업로드를 요청한다.
 */
MESSAGE prev_msg2;
void requestFileUpload(char* filepath)
{
	fprintf(stdout, "Request file upload...\n");
	MESSAGE msg;
	msg.type = REQUEST_UPLOAD;
	strcpy(msg.value, filepath);

	// prev_msg와 비교 후, 같은 경우에는 download 요청을 하지 않는다.
	if (prev_msg2.type == msg.type &&
			strcmp(prev_msg2.value, msg.value) == 0) {
		fprintf(stdout, "Skip send message..\n");
		return;
	}
	fprintf(stdout, "Send upload message...\n");
	prev_msg2.type = msg.type;
	strcpy(prev_msg2.value, msg.value);
	if (send_message(&msg) == false) {
		fprintf(stderr, "Failed to send PING to Operator. There might be connection problem.\n");
		_exit(FAILED_SEND_MSG);
	}
}

/*
 * send_message
 * 자바로 짜여진 Operator 인스턴스로 메세지를 보낸다.
 * 단순히 메세지를 구성하여 보내기만 하고 수신은 listening 쓰레드에서 수행한다.
 */
bool send_message(MESSAGE* msg) {
	// 이전 메세지와 같은 메세지는 보낼 수 없다.
	// 메세지 토큰을 보내 전송 시작을 알린다.
	MESSAGE_TYPE flag = MESSAGE_TOKEN;
	prepareBufferWithValue(buffer, (char *)flag, strlen(flag) * sizeof(char));
	if (write(sockfd_out, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Failed to send token.\n");
		return false;
	}

	// 타입 전송
	MESSAGE_TYPE type = msg->type;
	prepareBufferWithValue(buffer, (char *)type, strlen(type) * sizeof(char));
	strcat(buffer, ";;");	// 타입과 값 구분 위해 문자열 삽입
	if (write(sockfd_out, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Failed to send message type.\n");
		return false;
	}
	// 값 전송
	if (msg->value != NULL && strlen(msg->value) != 0) {
		prepareBufferWithValue(buffer, (char *)msg->value, MESSAGE_SIZE);
	}
	else {
		// 값이 유효하지 않은 경우
		prepareBufferWithValue(buffer, "nothing", MESSAGE_SIZE);
	}
	if (write(sockfd_out, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Failed to send message value.\n");
		return false;
	}
	// 메세지 토큰을 보내 전송 종료을 알린다.
	prepareBufferWithValue(buffer, (char *)flag, strlen(flag) * sizeof(char));
	if (write(sockfd_out, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Failed to send token.\n");
		return false;
	}

	return true;
}

/*
 * prepareBufferWithValue
 * 버퍼 사용을 위해 버퍼를 초기화하고 값을 복사한다.
 */
void prepareBufferWithValue(char* buffer, char* value, int size) {
	// 버퍼 초기화
    bzero(buffer, BUFFER_SIZE);
    strcpy(buffer, value);
    // 자바 프로그램에서 readLine() 을 호출하기 때문에 끝 부분에 개행문자를 삽입한다.
    strcat(buffer, "\n");
}
