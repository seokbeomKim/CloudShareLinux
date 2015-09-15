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

MESSAGE message;
char*	test_connection(MESSAGE *msg);
void	receiveAndHandleMessage(void);
bool	send_message(MESSAGE* msg);
void	synchronizeFileList(void);
// 소켓 통신에 사용되는 버퍼
char buffer[BUFFER_SIZE];

/*
 * 소켓 통신위ᅟ한 변수 선언
 */
int sockfd_in;
int sockfd_out;
struct sockaddr_in serv_addr;
struct hostent *server;


void* listening(void* arg)
{
	// 쓰레드 확인
	pthread_t id = pthread_self();
	fprintf(stdout, "Operator thread is running... id = %lu\n", id);
	// 소켓 연결 준비
    sockfd_in = socket(AF_INET, SOCK_STREAM, 0);
    sockfd_out = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_in < 0 || sockfd_out < 0) {
        perror("ERROR opening socket");
    }
	// 소켓 연결 대기
    server = gethostbyname("127.0.0.1");
    if (server == NULL) {
    	perror("ERROR, host not found\n");
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
	send_message(&msg);

	/*
	 * 프로그램 시작 전, 파일의 리스트를 초기화하여야 하기 때문에 이 부분을 여기서 처리한다.
	 * 파일 리스트의 동기화는 메타데이터파일(.CloudShare 디렉토리 내)의 동기화로 이루어지므로,
	 * 일차적으로 .CloudShare 디렉토리 내의 파일 리스트를 통해 readdir을 로드하도록 한다.
	 * 여기서 실행되는 초기화 명령을 통해 thread에서 메타파일을 비동기적으로 다운로드 하도록 한다.
	 */
	fprintf(stdout, "Synchronize file list...\n");
	synchronizeFileList();

	bzero(buffer, BUFFER_SIZE);
	// 연결이 성공하면 루프 돌면서 External Service와 통신한다.
	while (true) {
		receiveAndHandleMessage();
		sleep(1);
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
	if (read(sockfd_in, buffer, MESSAGE_SIZE) < 0) {
		fprintf(stderr, "Failed to receive message");
	}
	// 전송 받은 내용을 분석하여 메세지의 타입을 알아낸다.

	// 타입에 적절한 핸들러를 호출한다.
}

/*
 * 테스트하기 위한 함수 (처음에 서버로부터의 응답 확인)
 * 서버로 MESSAGE를 전송하고 응답을 받아 반환한다.
 */
char* test_connection(MESSAGE* msg) {
	// 먼저 메세지 타입을 보내고 준비된 서버에서 ACK을 받으면 값을 보낸다.
	// MESSAGE 타입 전송
	MESSAGE_TYPE type = msg->type;
	prepareBufferWithValue(buffer, (char *)type, strlen(type) * sizeof(char));
	if (write(sockfd_out, buffer, strlen(buffer)) < 0) {
		fprintf(stderr, "Failed to send message type.\n");
	}

    // 메세지 타입 전송 후에는 ACK을 받는다.
	bzero(buffer, BUFFER_SIZE);
	if (read(sockfd_in, buffer, MESSAGE_SIZE) < 0) {
		fprintf(stderr, "Failed to receive ACK message from external service.\n");
	}
	printf("Received ACK from type : %s", buffer);
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
	}
	// 값 전송 후에 ACK 받음
	bzero(buffer, BUFFER_SIZE);
	if (read(sockfd_in, buffer, BUFFER_SIZE) < 0) {
		fprintf(stderr, "Failed to receive ACK message from external service.\n");
	}
	printf("Received ACK from value : %s", buffer);

	return buffer;
}

/*
 * ============
 * FUSE 관련 함수
 * ============
 * FUSE 관련 함수에서 호출되는 함수들을 정의한다. 사용자가 요청한 작업은 해당 프로그램에서 수행하지 않고
 * 자바 프로그램으로 메세지를 보낸 후 수행한다.
 */
/*
 * synchronizeFileList
 * 연결되어 있는 클라이언트들에게 파일리스트를 요청하여 현재 로컬에 없는 파일 메타데이터를 다운로드하여
 * 파일리스트를 동기화한다.
 */
void synchronizeFileList(void)
{
	MESSAGE msg = {
			REQUEST_FILELIST,
			""
	};
	send_message(&msg);
}
/*
 * requestFileDownload
 */
void requestFileDownload(char* filepath)
{
	fprintf(stdout, "Request file download...\n");
	MESSAGE msg;
	msg.type = REQUEST_DOWNLOAD;
	strcpy(msg.value, filepath);
	send_message(&msg);
}

/*
 * requestFileUpload
 */
void requestFileUpload(char* filepath)
{
	fprintf(stdout, "Request file upload...\n");
	MESSAGE msg;
	msg.type = REQUEST_UPLOAD;
	strcpy(msg.value, filepath);
	send_message(&msg);
}

/*
 * send_message
 * 자바로 짜여진 Operator 인스턴스로 메세지를 보낸다.
 * 단순히 메세지를 구성하여 보내기만 하고 수신은 listening 쓰레드에서 수행한다.
 */
bool send_message(MESSAGE* msg) {
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
