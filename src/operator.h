/*
 * operator.h
 *
 *  Created on: Sep 6, 2015
 *      Author: Sukbeom Kim (chaoxifer@gmail.com)
 */

#ifndef SRC_OPERATOR_H_
#define SRC_OPERATOR_H_

#define PORT_NUM_IN 		7788		// 통신 포트(로컬간 소켓 연결)
#define PORT_NUM_OUT		7789		// 통신 포트(로컬간 소켓 연결)
#define WAIT_FOR_RETRY	3			// 재시작 하기위한 시간

void* listening(void* arg);
void prepareBufferWithValue(char* buffer, char* value, int size);

#endif /* SRC_OPERATOR_H_ */
