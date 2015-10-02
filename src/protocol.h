/*
 * protocol.h
 *
 *  Created on: Sep 7, 2015
 *      Author: Sukbeom Kim (chaoxifer@gmail.com)
 */

#ifndef SRC_PROTOCOL_H_
#define SRC_PROTOCOL_H_

/*
 * FUSE Mounter 프로그램과 Java based Operator간의 소켓 통신을 위한
 * 프로토콜을 정의한다.
 * Java의 Operator가 소켓을 통해 FUSE Mounter에 요청하는 방식으로 이루
 * 어지며 이 때 사용되는 Request 값들은 모두 여기에 정의한다.
 */
#define MESSAGE_SIZE	512
#define BUFFER_SIZE		512
typedef char* MESSAGE_TYPE;

#define MESSAGE_TOKEN		 "::__::"
#define MESSAGE_SPLITTER 	 ";;"
#define REQUEST_REFRESH	 	 "request_refresh"
#define ACK_REFRESH		 	 "ack_refresh"
#define REQUEST_FILELIST	 "request_FileList"
#define ACK_FILELIST		 "ack_FileList"
#define REQUEST_UPLOAD		 "request_Upload"
#define ACK_UPLOAD			 "ack_Upload"
#define REQUEST_DOWNLOAD	 "request_Download"
#define ACK_DOWNLOAD		 "ack_Download"
#define REQUEST_CHECK_CONNECTION	 "request_checkconnection"
#define ACK_CHECK_CONNECTION		 "ack_checkconnection"

/*
 * IPC Message 핸들러
 */
#define MAX_HANDLER		16		// 핸들러의 갯수
typedef enum {
	REFRESH,
	FILELIST,
	UPLOAD,
	DOWNLOAD,
	CHK_CONN,
} OPERATION;

/*
 * 주고 받는 메세지 구조체 정의
 */
typedef struct {
	MESSAGE_TYPE type;
	char value[MESSAGE_SIZE];
} MESSAGE;

#endif /* SRC_PROTOCOL_H_ */
