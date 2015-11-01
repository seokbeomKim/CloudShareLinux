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
#include "error_code.h"

pthread_t operator_tid;	// operator thread id
pthread_t main_thread;

/*
 * 파일 및 디렉토리 구조
 * Metadata가 저장되는 디렉토리(local)와 실제 클라우드가 마운트되는 디렉토리가 존재한다.
 * $HOME/.CloudShare에는 각 파일의 메타데이터가 들어있으며 (분할된 파일 정보가 다운로드 가능한 url),
 * 실제 사용자에게 보여지는 파일은 CloudShare부분이다.
 *
 */
#define CLOUD_DIRPATH 				"/Ndrive/CloudShare"		// 클라우드가 마운트되는 위치
#define FUSE_MOUNTER_CDIRPATH		"/.CloudShare"	// CloudShare 각 파일의 메타데이터 저장되는 캐시 directory 위치
#define FUSE_MOUNTER_DIRPATH		"/CloudShare"	// CloudShare 마운트되는 위치 (유저에게 보여지는 공간)
#define FUSE_MOUNTER_DWLPATH		"/Download"		// CloudShare에서 사용자가 선택한 파일이 다운로드될 위치
#define CLOUDSHARE_SUFFIX			".cs"

char*	home_dir; // 홈 디렉토리
char 	cache_dir[BUFFER_SIZE];	// 메타데이터 저장될 위치 (CDIRPATH)
char	mountpoint[BUFFER_SIZE]; // 유저에게 보여줄 마운트 포인트

extern void requestFileDownload(char *);
extern void requestFileUpload(char *);

static void get_cache_path(char *dst, const char* path, int size);

static char str[BUFFER_SIZE];

static bool reading = false;
static bool writing = false;

bool isOperatorRunning;



/* ==============================================================
 * Helper functions
 * ==============================================================
 */

/* ==============================================================
 * remove_cs_suffix
 * 파일 이름이 metadata 파일의 경우 *.cs파일로 되어있기 때문에 이 부분을
 * 문자열에서 제거한다.
 * readdir에서 파일 리스트를 보여줄 때 사용된다.
 * ==============================================================
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

/* ==============================================================
 * isCsFile
 * *.ᅟcs파일인지 확인한다.
 * true이면 cs파일, false이면 cs파일이 아니다.
 * ==============================================================
 */
bool isCsFile(const char* path) {
	char *suffix = CLOUDSHARE_SUFFIX;
	char *bname;
	char *path2 = strdup(path);
	char temp[BUFFER_SIZE];
	get_cache_path(temp, path, BUFFER_SIZE);

	bname = basename(path2);

	int len = strlen(bname);
	int i;
//	fprintf(stdout, "[isCsFile] path = %s and basename is %s\n", path, bname);

	bool b = true;	// 만약 파일 이름에 확장자가 있다면 true, 없다면 false
	for (i = 0; i < strlen(suffix); i++) {
		if(bname[len-1-i] != suffix[strlen(suffix) - 1 - i]) {
			b = false;
		}
	}
	return b;
}

/*
 * ==============================================================
 * get_cache_path
 * path로 전달된 파일경로를 캐쉬디렉토리 경로로 바꿔준다.
 * path: 파일 경로, dst: 실제 파일 경로(캐쉬 디렉토리 안), size: dst의 크기
 *
 * FUSE에서 전달되는 path의 형태는 /a.txt /b/b.txt 와 같다.
 * 실제 캐쉬 디렉토리에 저장되는 파일은 $(cache_dir)/a.txt.cs 와 같은 형태이기 때문에
 * 경로를 바꿔주어야 한다.
 * ==============================================================
 */
void get_cache_path(char *dst, const char* path, int size) {
	memset(dst, 0, size);
	strcpy(dst, cache_dir);
	strcat(dst, path);
}

/* ==============================================================
 * FUSE 관련 함수 구현 부분 시작
 * ==============================================================
 */

/*
 * ==============================================================
 * cs_getattr: 시스템콜 getattr이 호출될 때 호출되는 함수
 * 숨김파일은 표시하지 않으며 *.cs파일이 아닌 경우에 한해서 원래 attribute
 * 를 보여준다. 이유는, 사용자가 파일브라우저를 통해 write를 할 때
 * overwrite 여부를 브라우저에서 파일 attribute로 판단하는데, 임의로 수
 * 정하게 되는 경우 overwrite를 해야하는 것으로 판단되기 때문이다.
 * 또한 cs 파일의 경우, Java-based 프로그램 (Operator)에서 생성하기 때
 * 문에 임의로 설정해도 무방하다.
 * ==============================================================
 */
static int cs_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	char temp[BUFFER_SIZE];
	get_cache_path(temp, path, BUFFER_SIZE);

	char *bname;
	char *path2 = strdup(path);
	bname = basename(path2);

	// 숨김파일은 없는 파일로 처리
	if (bname[0] == '.')
		return -ENOENT;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
			stbuf->st_mode = S_IFDIR | 0755;
	}
	else {
		if (isCsFile((char *)path)) {
			// CS file인 경우 BUFFER_SIZE (메세지 크기)만큼으로
			// 파일 크기를 설정한다.
			stbuf->st_size = BUFFER_SIZE;
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;
		}
		else {
			res = lstat(temp, stbuf);
		}
	}
	if (res == -1)
		return -errno;

	return 0;
}

/* ==============================================================
 * cs_readdir
 * 캐쉬 디렉토리에 있는 메타데이터 파일들을 중심으로 파일리스트를 보여준다.
 * ==============================================================
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
			// *.cs파일을 제거하게 되면 확장자가 드러나게 되어 파일 처리하기 힘들어짐.
			// cs확장자를 두되 텍스트편집기가 열리도록 하여 파일 다운로드를 알림.
			// 숨김파일은 보여지지 않는다.
			if (isCsFile(dptr->d_name)) {
				filler(buf, dptr->d_name, NULL, 0);
			}
		}
	}
	closedir(dp);
	return 0;
}

/* ==============================================================
 * cs_open
 * 파일 열기 대신에 메타데이터 파일의 url을 중심으로 다운로드하도록 메세지를 보낸다.
 * (실제 작업은 자바의 Operator에서 수행한다.)
 * ==============================================================
 */
static int cs_open(const char *path, struct fuse_file_info *fi)
{
	// 숨김파일 처리된 것은 무시한다.
	char *bname;
	char *path2 = strdup(path);
	bname = basename(path2);
	if (bname[0] == '.') {
		return -ENOENT;
	}
	// gvfs 로 인해 생기는 파일들 리스트에서 제거
	else if (strcmp(bname, "autorun.inf") == 0) {
		return -ENOENT;
	}

	return 0;
}

/* ==============================================================
 * cs_read: 파일 읽기에 호출되는 함수
 * ==============================================================
 */
static int cs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	// *.cs 파일이 아닌 파일은 일반적인 read 수행한다.
	char temp[BUFFER_SIZE];
	get_cache_path(temp, path, 256);

	if (!isCsFile((char *)path)) {
		// cs 파일이 아닌 일반 파일인 경우, 일반적인 읽기를 수행한다.
		// 다만, fi 가 가지고 있는 파일디스크립터를 수정할 필요가 있다.
		// cache_directory에 있는 파일을 읽을 수 있도록 해주어야함.
        int res;
        (void) path;
        (void) fi;
        int fd = open(temp, O_RDONLY);
        fi->fh = fd;
        res = pread(fi->fh, buf, size, offset);
        if (res == -1)
			res = -errno;
        return res;
	}
	// ==============================================================
	// 메타파일인 경우 (*.cs)
	// ==============================================================
	// 사용자에게 보여주기 위한 정보들
	// 파일 이름
	reading = true;
	printf("[read] Read a meta file at %s\n", path);
	char *bname;
	char *path2 = strdup(path);
	bname = basename(path2);

	// 다운로드 디렉토리
	struct passwd *pw = getpwuid(getuid());
	const char *homedir = pw->pw_dir;

	size_t len;
	(void) fi;

	// 사용자가 파일을 열었을 때 보여질 메세지
	sprintf(str, "Your file (%s) will be downloaded at %s/Download directory.\n"
			"File Info: \n",
			bname, homedir);
	len = strlen(str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, str + offset, size);
	} else
		size = 0;

	// 함수를 끝내기 전에 Operator에 메세지를 보낸다.
	requestFileDownload(temp);

	return size;
}

/*
 * ==============================================================
 * cs_unlink: 파일 삭제시 호출되는 unlink 관련 함수
 * ==============================================================
 *
 */
static int cs_unlink(const char *path)
{
	printf("[unlink] Remove the file at %s\n", path);
	int res;
	char temp[256];
	get_cache_path(temp, path, 256);
	res = unlink(temp);

	requestFileUnlink(temp);

	if (res == -1)
		return -errno;
	return 0;
}

/*
 * ==============================================================
 * cs_statfs: 파일로부터 stat구조체 반환
 * CloudShare(마운트 포인트)에 담겨있는 파일은 가상의 파일이기 때문에
 * 실질적으로 읽어올 파일(.CloudShare)을 이용해 statfs을 호출한다.
 * ==============================================================
 *
 */
static int cs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	char temp[256];
	get_cache_path(temp, path, 256);
	res = statvfs(temp, stbuf);
	if (res == -1)
		return -errno;
	return 0;
}

/*
 * ==============================================================
 * cs_write: 파일 쓰기 때 호출되는 함수
 * 마운트 포인트가 아닌 cache directory에 파일 쓰기를 한다.
 * ==============================================================
 *
 */
static int cs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
	/*
	 * write한 경우 Java based 프로그램에게 경로를 알리고
	 * 파일 업로드 후에 Operator에서 메타 데이터 파일을 만든 후
	 * copy된 파일을 삭제한다.
	 */
	int res;
	(void) path;
	char temp[256];
	get_cache_path(temp, path, 256);
	// fi->fh는 마운트포인트에서의 파일을 가리키고 있기 때문에
	// 파일 디스크립터를 바꿔주어야한다.
	int fd = open(temp, O_RDWR);
	fi->fh = fd;
	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;
	writing = true;

	return res;
}

/*
 * ==============================================================
 * cs_mknod: 노드 생성 함수 (write와 함께 실행)
 * ==============================================================
 */
static int cs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	char temp[256];
	get_cache_path(temp, path, 256);
	if (S_ISFIFO(mode))
		res = mkfifo(temp, mode);
	else
		res = mknod(temp, mode, rdev);
	if (res == -1)
		return -errno;
	return 0;
}

static int cs_flush(const char *path, struct fuse_file_info *fi)
{
	int res;
	(void) path;
	char temp[256];
	get_cache_path(temp, path, 256);

	if ((fi->flags & 3) != O_WRONLY) {
		if (!reading && writing) {
			requestFileUpload(temp);
		}
	}
	res = close(dup(fi->fh));

	if (reading) {
		reading = false;
	}
	if (writing) {
		writing = false;
	}
	if (res == -1)
		return -errno;
	return 0;
}

static struct fuse_operations cloudshare_oper = {
	.getattr        = cs_getattr,
	.readdir        = cs_readdir,
	.open           = cs_open,
	.read			= cs_read,
	.unlink			= cs_unlink,
	.statfs			= cs_statfs,
	.write			= cs_write,
	.mknod			= cs_mknod,
	.flush			= cs_flush,
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
		fprintf(stderr, "Failed to run command\n" );
		_exit(CLOUD_NOT_MOUNTED);
	}
	while (fgets(o_cmd, sizeof(o_cmd)-1, fp) != NULL) {
		// 출력 부분 확인
		o_c = atoi(o_cmd);
	}
	/* Close file descriptor */
	pclose(fp);
	// 만약 클라우드가 마운트되어있지 않다면 프로그램을 종료한다.
	if (o_c == 0) {
		fprintf(stderr, "Cloud drive is not mounted. (Ndrive)\n");
		_exit(CLOUD_NOT_MOUNTED);
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
		fprintf(stdout, "Cache directory exists. \n");
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
		_exit(PTHREAD_ERROR);
    }

	err = pthread_create(&operator_tid, &attr, &listening, NULL);
	if (err != 0) {
		fprintf(stdout, "Can't create thread: %s\n", strerror(err));
	}

	// 실행하기 전에 성공적으로 Operator가 실행되고 있는지 확인하고 확인되면 FUSE를 실행
	// 아닌 경우에는 프로그램을 종료한다.
	sleep(WAIT_FOR_RETRY);
	if (!isOperatorRunning) {
		// 쓰레드가 살아있는지 확인하고 없다면
		fprintf(stderr, "No thread...exit the program.\n");
		pthread_cancel(operator_tid);
		exit(1);
	}

	// FUSE_main 쓰레드를 실행한다.
	int status;
	int ret = 0;

	ret = fuse_main(argc, argv, &cloudshare_oper, NULL);
	pthread_cancel(operator_tid);
	pthread_join(operator_tid, (void **)&status);

	return ret;
}
