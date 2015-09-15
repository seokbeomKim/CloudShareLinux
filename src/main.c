/*
 * main.c
 *
 *  Created on: Sep 6, 2015
 *      Author: Sukbeom Kim (chaoxifer@gmail.com)
 */

#define FUSE_USE_VERSION 30
#define BUFFER_SIZE		512

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <netdb.h>
#include <pwd.h>
#include "operator.h"

pthread_t operator_tid;	// operator thread id
int shared_value = 0;

/*
 * 파일 및 디렉토리 구조
 * Metadata가 저장되는 디렉토리(local)와 실제 클라우드가 마운트되는 디렉토리가 존재한다.
 * $HOME/.CloudShare에는 각 파일의 메타데이터가 들어있으며 (분할된 파일 정보가 다운로드 가능한 url),
 * 실제 사용자에게 보여지는 파일은 CloudShare부분이다.
 *
 */
#define CLOUD_DIRPATH 				"/Ndrive/CloudShare"		// 클라우드가 마운트되는 위치
#define FUSE_MOUNTER_CDIRPATH		"/.CloudShare"	// CloudShare 각 파일의 메타데이터 저장되는 캐시 directory 위치
#define FUSE_MOUNTER_DIRPATH			"/CloudShare"	// CloudShare 마운트되는 위치 (유저에게 보여지는 공간)
#define CLOUDSHARE_SUFFIX			".cs"

char* home_dir; // 홈 디렉토리
char cache_dir[BUFFER_SIZE];	// 메타데이터 저장될 위치 (CDIRPATH)
char mountpoint[BUFFER_SIZE]; // 유저에게 보여줄 마운트 포인트

extern void requestFileDownload(char *);
extern void requestFileUpload(char *);

/*
 * remove_cs_suffix
 * 파일 이름이 metadata 파일의 경우 *.cs파일로 되어있기 때문에 이 부분을 문자열에서 제거한다.
 * readdir에서 파일 리스트를 보여줄 때 사용된다.
 */
char* remove_cs_suffix(char* filename) {
	char *suffix = CLOUDSHARE_SUFFIX;
	// filename 뒤쪽부터 확인한다.
	int len = strlen(filename);
	int i;

	bool b = true;	// 만약 파일 이름에 확장자가 있다면 true, 없다면 false
	for (i = 0; i < strlen(suffix); i++) {
		if(filename[len-1-i] != suffix[strlen(suffix) - 1 - i]) {
			b = false;
		}
	}
	if (b) {
		filename[len-strlen(suffix)] = '\0';
	}
	return filename;
}

/*
 * get_cache_path
 * path로 전달된 파일경로를 캐쉬디렉토리 경로로 바꿔준다.
 * path: 파일 경로, dst: 실제 파일 경로(캐쉬 디렉토리 안), size: dst의 크기
 *
 * FUSE에서 전달되는 path의 형태는 /a.txt /b/b.txt 와 같다.
 * 실제 캐쉬 디렉토리에 저장되는 파일은 $(cache_dir)/a.txt.cs 와 같은 형태이기 때문에
 * 경로를 바꿔주어야 한다.
 */
void get_cache_path(char *dst, const char* path, int size) {
	memset(dst, 0, size);
	strcpy(dst, cache_dir);
	strcat(dst, path);
	strcat(dst, CLOUDSHARE_SUFFIX);
}

/*
 * FUSE 관련 함수 구현 부분 시작
 */
static int cs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	char temp[512];
	get_cache_path(temp, path, 512);

	char *bname;
	char *path2 = strdup(path);
	bname = basename(path2);

	// .xxx 타입은 생성하지 않는다.
	if (bname[0] == '.')
		return -ENOENT;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
			stbuf->st_mode = S_IFDIR | 0755;
	}
	else {
		stbuf->st_mode = S_IFREG | 0644;
		int fd = open(temp, 0644);
		if (fd < 0) {
			return -ENOENT;
		}
	}
	return res;
}

/*
 * readdir
 * 캐쉬 디렉토리에 있는 메타데이터 파일들을 중심으로 파일리스트를 보여준다.
 */
static int cs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
	// 기본 엔트리
	(void) offset;
	(void) fi;
	if (strcmp(path, "/") != 0) {
		return 0;
	}
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	// cache directory의 파일리스트를 얻어 가공한 뒤에 리스트로 보여준다.
	DIR *dp = NULL;
	struct dirent *dptr = NULL;

	dp = opendir(cache_dir);
	if (dp < 0 || dp == NULL) {
		fprintf(stderr, "Failed to open directory: %s\n", cache_dir);
	}
	while ((dptr = readdir(dp)) != NULL) {
		if (dptr->d_type == DT_REG) {
			// 일반 파일에 해당하는 경우만 리스트에 넣는다.
			// 이 때, 파일 끝의 .cs 확장자 부분을 제거하여 보여준다.
			filler(buf, remove_cs_suffix(dptr->d_name), NULL, 0);
		}
	}
	closedir(dp);
	return 0;
}

/*
 * cloudshare_open
 * 파일 열기 대신에 메타데이터 파일의 url을 중심으로 다운로드하도록 메세지를 보낸다.
 * (실제 작업은 자바의 Operator에서 수행한다.)
 */
static int cs_open(const char *path, struct fuse_file_info *fi)
{
	// 숨김파일 처리된 것은 무시한다.
	char temp[256];
	char *bname;
	char *path2 = strdup(path);
	bname = basename(path2);

	if (bname[0] == '.') {
		return -ENOENT;
	}
	// gvfs 로 인해 생기는 파일들 제거
	else if (strcmp(bname, "autorun.inf") == 0) {
		return -ENOENT;
	}
	get_cache_path(temp, path, 256);
	fprintf(stdout, "[open] path = %s\n", temp);

	// 파일 존재 유무에 때라 다운로드 여부 결정을 한다.
	int fd = open(remove_cs_suffix(temp), O_RDONLY);
	if (fd < 0) {
		requestFileDownload(temp);
	}
	return 0;
}

/*
 * cs_write
 * 실제로 파일의 내용을 write하지 않고 size를 리턴하여 write를 바로 종료한다.
 */
static int cs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
	int fd;
	char temp[256];
	get_cache_path(temp, path, 256);
	printf("[write] path = %s\n", temp);
	fd = open(temp, O_WRONLY);
	if (fd == -1)
		return -errno;
	close(fd);
	return size;
}

static int cs_utimens(const char *path, const struct timespec ts[2])
{
	/* don't use utime/utimes since they follow symlinks */
	return 0;
}

/*
 * cs_mknod
 * 새로운 파일을 생성하는 부분
 * 사용자가 파일 브라우저에서 파일을 업로드 하는 부분이므로 여기서 Operator에 파일 업로드를 요청한다.
 */
static int cs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	char temp[256];
	get_cache_path(temp, path, 256);
	printf("[mknod] %s\n",temp);

	res = mknod(temp, mode, rdev);
	requestFileUpload(temp);
	return res;
}

static struct fuse_operations cloudshare_oper = {
        .getattr        = cs_getattr,
        .readdir        = cs_readdir,
        .open           = cs_open,
        .utimens        = cs_utimens,
		.mknod			= cs_mknod,
		.write			= cs_write,
};

/*
 * main 개괄적인 프로세스
 * 클라우드가 마운트된 디렉토리와 CloudShare용 디렉토리로 나뉜다.
 * 예를 들어, Ndrive를 ~/Ndrive에 마운트하였고 실질적인 파일이 ~/Ndrive/CloudShare 디렉토리 내에 저장된다고 해도
 * 해당 파일들에 대한 metadata를 구성하는 파일은 ~/CloudShare에 존재한다.
 * 구체적으로, Ndrive/CloudShare/a.txt.2 파일이 있다고 치면 ~/CloudShare/a.txt.2 파일은 해당 파일에 대한
 * 클라우드 상에서의 url정보를 담고 있다. (prev-current-next 혹은 모든 파일 구성의 url)
 * 실질적으로 FUSE-Mounter를 통해 마운트되는 디렉토리는 CloudShare (metadata정보 모음) 디렉토리가 되며, FUSE를 사용하여
 * 파일 브라우저에서 표기되는 파일들의 목록들은 파일들의 이름에 불과하다.
 *
 * 프로그램 실행은 ./CloudShare [mount point] -f 이다. mount point는 ~/CloudShare 와 같이 준다.
 * -f 옵션을 주지 않으면 fuse_main이 main thread에서 _exit(0)을 호출하기 때문에 옵션을 주어야 정상작동한다.
 */

int main(int argc, char *argv[])
{
	FILE*	fp;
	char	o_cmd[128];	// 명령어 출력 버퍼
	int		o_c;		// number of outputs
	// 클라우드 드라이브 마운트 상태를 확인한다.
	fp = popen("mount | grep -i ndrivefuse | wc -l", "r");
	if (fp == NULL) {
		printf("Failed to run command\n" );
		_exit(1);
	}
	while (fgets(o_cmd, sizeof(o_cmd)-1, fp) != NULL) {
		// 출력 부분 확인
		o_c = atoi(o_cmd);
		printf("cmd output : %d\n", o_c);
	}
	/* Close file descriptor */
	pclose(fp);
	// 만약 클라우드가 마운트되어있지 않다면 프로그램을 종료한다.
	if (o_c == 0) {
		fprintf(stderr, "Cloud drive is not mounted. (Ndrive)\n");
		_exit(1);
	}
	// 클라우드가 마운트되어 있다면 각 필수 디렉토리를 검사하고 생성한다.
	if ((home_dir = getenv("HOME")) == NULL) {
	    home_dir = getpwuid(getuid())->pw_dir;
	}
	// CloudShare 관련 디렉토리 존재 여부 확인
	struct stat s;
	// Cache Directory 확인
	strcat(cache_dir, home_dir);
	strcat(cache_dir, FUSE_MOUNTER_CDIRPATH);
	int err = stat(cache_dir, &s);
	if (err < 0) {
		fprintf(stderr, "Cache directory(%s) doesn't exist. Create new directory..\n", cache_dir);
		// 디렉토리가 존재하지 않을 경우 새로 생성한다.
		mkdir(cache_dir, 0755);
	}
	else {
		fprintf(stdout, "Cache directory\t[EXIST]\n");
	}
	// 마운트 포인트 디렉토리 확인
	strcat(mountpoint, home_dir);
	strcat(mountpoint, FUSE_MOUNTER_DIRPATH);
	err = stat(mountpoint, &s);
	if (err < 0) {
		fprintf(stderr, "Mount point directory doesn't exist. Create new directory..");
		// 디렉토리가 존재하지 않을 경우 새로 생성한다.
		mkdir(mountpoint, 0755);
	}

	// 관련 디렉토리가 모두 준비되면 fuse mounter의 socket통신용(IPC)쓰레드를 실행한다.
    pthread_attr_t attr;
    errno = pthread_attr_init(&attr);
    if (errno) {
		perror("pthread_attr_init");
		_exit(1);
    }
	err = pthread_create(&operator_tid, &attr, &listening, NULL);
	if (err != 0) {
		fprintf(stdout, "Can't create thread: %s\n", strerror(err));
	}

	// FUSE_main 쓰레드를 실행한다.
	int status;
	int ret = 0;
	ret = fuse_main(argc, argv, &cloudshare_oper, NULL);
	pthread_cancel(operator_tid);
	pthread_join(operator_tid, (void **)&status);

	return ret;
}
