#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#define ROOT_DEVICE "sdb"
#define ROOT_FS_TYPE "ext3"
#define INIT_NAME "/sbin/init"


#define MINORBITS       20
#define MINORMASK       ((1U << MINORBITS) - 1)

#define MAJOR(dev)      ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)      ((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi)    (((ma) << MINORBITS) | (mi))

#define _SYSCALL_WR_NUM(x) # x
#define _SYSCALL_TEXTNUM(x) _SYSCALL_WR_NUM(x)
#define __SYSCALL(x, pref, suff) asm volatile( pref "mov r7, #" _SYSCALL_WR_NUM(x) "\nsvc 0\n" suff "mov pc, lr\n" )
#define _SYSCALL(x) __SYSCALL(x, "", "")
#define __syscall __attribute__((naked))
#define __init __attribute__((naked))


void print(const char *t);
void prepare(void);
void getprocname(const char *id, char *name, int len);
int isdecnum(const char *t);
int dectoint(const char *t);
void strappend(char *dst, const char *src);
int _strcmp(const char *a, const char *b);
__syscall int s_write(int fd, const void *buf, int l);
__syscall int s_read(int fd, void *buf, int l);
__syscall int s_execve(const char *path, char *const argv[], char *const envp[]);
__syscall int s_mount(const char *dev, const char *path, const char *filesystem, int flags, const void *data);
__syscall int s_umount(const char *path);
__syscall int s_getdents(int fd, struct dirent *d, int count);
__syscall int s_setpriority(int which, int who, int prior);
__syscall int s_open(const char *path, int flags);
__syscall int s_close(int fd);
__syscall int s_chroot(const char *path);
__syscall int s_mknod(const char *path, int mode, int dev);
__syscall int s_pause(void);
int s_usleep(int usec);
__syscall int s_nanosleep(struct timespec *req, struct timespec *rem);
__init void _start(void);


int main(int argc, char *const argv[], char *const envp[]) {
	prepare();
	s_execve(INIT_NAME, argv, envp);
}

void prepare(void) {
	char name[256];
	char dirents[16384];
	struct dirent *d;
	int pid;
	int fd;
	int maj;
	int min;
	int i;
	int n;

	print("* Mounting /proc\n");
	i = s_mount("proc", "/proc", "proc", MS_NOATIME, NULL);
	if(i<0) {
	   print("Warning: Unable to mount /proc\n");
	}
	
	print("* Mounting /sys\n");
	i = s_mount("sysfs", "/sys", "sysfs", MS_NOATIME, NULL);
	if(i<0) {
	   print("Warning: Unable to mount /sys\n");
	}
	
	print("* Mounting tmpfs on /dev\n");
	i = s_mount("tmpfs", "/dev", "tmpfs", 0, NULL);
	if(i<0) {
	   print("Warning: Unable to mount /dev\n");
	}
	
	fd = s_open("/proc", O_RDONLY | O_DIRECTORY | O_NONBLOCK);
	if(fd<0) {
	   print("Warning: Unable to open /proc\n");
	}
	
	d = (struct dirent*) &dirents[0];
	n = s_getdents(fd, d, sizeof(dirents));

	if(fd>=0) s_close(fd);
	
	pid = 0;
	for(i=0; i<n; i+=d->d_reclen) {
	   d = (struct dirent*) &dirents[i];
	   if(d->d_type==DT_DIR || d->d_type==DT_UNKNOWN) {
	      if(isdecnum(d->d_name)) {
	         getprocname(d->d_name, name, sizeof(name));
		 if(name[0]==0) continue;
		 if(_strcmp(name, "adc_keypad")==0) {
		    pid = dectoint(d->d_name);
		    break;
		 }
	      }
	   }
	}

	if(pid) {
	   print("* Applying workaround for adc_keypad (set nice to 20)\n");
	   n = s_setpriority(pid, PRIO_PROCESS, 20);
	   if(n<0) {
	      print("Warning: Unable to change the priority\n");
	   }
	}

	print("* Waiting for root device\n");
	name[0] = 0;
	strappend(name, "/sys/class/block/");
	strappend(name, ROOT_DEVICE);
	strappend(name, "/dev");
	fd = -1;
	n = -1;
	for(i=0; i<100; i++) {
	   fd = s_open(name, O_RDONLY);
	   if(fd>0) break;
	   s_usleep(100000);
	}
	if(fd>=0) n = s_read(fd, name, sizeof(name));
	
	maj = -1;
	if(n>0) {
	   name[n] = 0;
	   for(i=0; name[i]; i++) {
	      if(name[i]=='\n') {
	         name[i] = 0;
	         break;
	      }
	   }

	   for(i=0; name[i]; i++) {
	      if(name[i]==':') break;
	   }

	   if(name[i]) {
	      name[i] = 0;
	      maj = dectoint(name);
	      min = dectoint(&name[i+1]);
	   }
	}

	if(maj>0) {
	   print("* Creating root device in /dev\n");
	   name[0] = 0;
	   strappend(name, "/dev/");
	   strappend(name, ROOT_DEVICE);
	   print(name);
	   i = s_mknod(name, S_IFBLK | 0660, maj<<8 | min);
	   if(i<0) {
	      print("Error: Unable to create root device node\n");
	      s_pause();
	   }
	} else {
	   print("Error: Unable to find root device\n");
	   s_pause();
	}
	
	print("* Mounting root filesystem on /real_root\n");
	i = s_mount(name, "/real_root", ROOT_FS_TYPE, MS_RDONLY, "");
	if(i<0) {
	   print("Error: Unable to mount root filesystem\n");
	   s_pause();
	}

	print("* Unmounting filesystems\n");
	s_umount("/sys");
	s_umount("/proc");
	s_umount("/dev");

	print("* Changing root directory to /real_root\n");
	i = s_chroot("/real_root");
	if(i<0) {
	   print("Error: Unable to change root directory\n");
	   s_pause();
	}

	print("* Executing init\n");
}

int isdecnum(const char *t) {
	while(*t) {
	   if(*t<'0' || *t>'9') return 0;
	   t++;
	}
	return 1;
}

int dectoint(const char *t) {
	//int a;
	//int r;
	//r = 0;
	/*while(*t) {
	   //if((*t)<'0' || (*t)>'9') return -1;
	   //r *= 10;
	   //r += (*t) - '0';
	   t++;
	}*/
	return 1;
}

void getprocname(const char *id, char *name, int len) {
	char fname[256];
	char buf[256];
	char *pname;
	int n;
	int fd;

	name[0] = 0;
	fname[0] = 0;
	strappend(fname, "/proc/");
	strappend(fname, id);
	strappend(fname, "/stat");

	fd = s_open(fname, O_RDONLY);
	if(fd<0) return;

	n = s_read(fd, buf, sizeof(buf));
	if(n<=0) return;
	buf[n] = 0;

	for(pname=buf; *pname; pname++) {
	   if(*pname==' ') {
	      pname++;
	      break;
	   }
	}

	if(pname[0]==0) return;
	if(pname[0]=='(') pname++;

	for(n=0; pname[n]; n++) {
	   if(n>=len) break;
	   if(pname[n]==' ') break;
	   name[n] = pname[n];
	}
	if(n) {
	   if(name[n-1]==')') n--;
	}

	name[n] = 0;
}


void strappend(char *dst, const char *src) {
	while(*dst) dst++;
	while(*src) {
	   *dst = *src;
	   dst++;
	   src++;
	}
	*dst = 0;
}

int _strcmp(const char *a, const char *b) {
	while(*a) {
	   if(*a!=*b) return 1;
	   a++;
	   b++;
	}
	return 0;
}


void print(const char *t) {
	int n;
	n = 0;
	while(t[n]) n++;
	s_write(1, t, n);
}

int s_write(int fd, const void *buf, int l) {
	_SYSCALL(SYS_write);
}

int s_read(int fd, void *buf, int l) {
	_SYSCALL(SYS_read);
}

int s_execve(const char *path, char *const argv[], char *const envp[]) {
	_SYSCALL(SYS_execve);
}

int s_mount(const char *dev, const char *path, const char *filesystem, int flags, const void *data) {
	__SYSCALL(SYS_mount, "push {r4}\nldr r4, [sp, #4]\n", "pop {r4}\n");
}

int s_getdents(int fd, struct dirent *d, int count) {
	_SYSCALL(SYS_getdents);
}

int s_setpriority(int which, int who, int prior) {
	_SYSCALL(SYS_setpriority);
}

int s_open(const char *path, int flags) {
	_SYSCALL(SYS_open);
}

int s_close(int fd) {
	_SYSCALL(SYS_close);
}

int s_chroot(const char *path) {
	_SYSCALL(SYS_chroot);
}

int s_mknod(const char *path, int mode, int dev) {
	_SYSCALL(SYS_mknod);
}

int  s_pause(void) {
	_SYSCALL(SYS_pause);
}

int s_umount(const char *path) {
	_SYSCALL(SYS_umount2);
}

int s_usleep(int usec) {
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = usec*1000;
	return s_nanosleep(&t, NULL);
}

int s_nanosleep(struct timespec *req, struct timespec *rem) {
	_SYSCALL(SYS_nanosleep);
}

void _start(void) {
	asm volatile ( 	"mov fp, #0\n"
			"mov lr, #0\n"
			"b main\n"
			"mov r7, #" _SYSCALL_TEXTNUM(SYS_exit) "\n"
			"svc 0\n");
}
