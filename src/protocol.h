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
typedef const char* MESSAGE_TYPE;

MESSAGE_TYPE MESSAGE_TOKEN		= "::__::";
MESSAGE_TYPE MESSAGE_SPLITTER 	= ";;";
MESSAGE_TYPE REQUEST_REFRESH	= "request_refresh";
MESSAGE_TYPE ACK_REFRESH		= "ack_refresh";
MESSAGE_TYPE REQUEST_FILELIST	= "request_filelist";
MESSAGE_TYPE ACK_FILELIST		= "ack_filelist";
MESSAGE_TYPE REQUEST_UPLOAD		= "request_upload";
MESSAGE_TYPE ACK_UPLOAD			= "ack_upload";
MESSAGE_TYPE REQUEST_DOWNLOAD	= "request_download";
MESSAGE_TYPE ACK_DOWNLOAD		= "ack_download";
MESSAGE_TYPE REQUEST_CHECK_CONNECTION	= "request_checkconnection";
MESSAGE_TYPE ACK_CHECK_CONNECTION		= "ack_checkconnection";

/*
 * 주고 받는 메세지 구조체 정의
 */
typedef struct {
	MESSAGE_TYPE type;
	char value[MESSAGE_SIZE];
} MESSAGE;

#endif /* SRC_PROTOCOL_H_ */
