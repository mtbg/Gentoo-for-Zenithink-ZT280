#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>


#define CPIO_PATTERN "070701........................................................................................................"
#define CPIO_END CPIO_PATTERN "TRAILER!!!"

int find_cpio(void *buf, int len, int e);
int find_initrd(void *kern, int len, int *initrd_pos, int *initrd_len);
void extract_swap_initrd(int swap, const char *kernel, const char *initrd, const char *output);
void usage(const char *pname);

int main(int argc, char **argv) {
	if(argc==1) {
	   usage(argv[0]);
	   return 0;
	}
	if(strcmp(argv[1], "extract")==0) {
	   if(argc<=3) {
	      fprintf(stderr, "extract: missing arguments\n");
	      usage(argv[0]);
	      return 1;
	   }
	   extract_swap_initrd(0, argv[2], argv[3], NULL);
	} else if(strcmp(argv[1], "swap")==0) {
	   if(argc<=4) {
	      fprintf(stderr, "swap: missing arguments\n");
	      usage(argv[0]);
	      return 1;
	   }
	   extract_swap_initrd(1, argv[2], argv[3], argv[4]);
	} else {
	   fprintf(stderr, "Unknown command: %s\n", argv[1]);
	   usage(argv[0]);
	   return 1;
	}
	return 0;
}

void usage(const char *pname) {
	printf("Usage: %s command [arguments...]\n", pname);
	printf("Initrd manipulation tool for uncompressed kernel binary\n");
	printf("Available commands:\n");
	printf("\textract <kernel> <output>         - tries to extract initrd from a kernel binary\n");
	printf("\tswap <kernel> <initrd> <output>   - swaps kernel initrd (if possible)\n");
}

int find_cpio(void *buf, int len, int e) {
	static const char cpio_pattern[] = CPIO_PATTERN;
	static const char cpio_end[] = CPIO_END;
	char *p = buf;
	int pattern_l;
	const char *pattern;
	char a;
	int i;
	int j;
	int f;

	if(e) {
	   pattern = cpio_end;
	   pattern_l = sizeof(cpio_end)-1;
	} else {
	   pattern = cpio_pattern;
	   pattern_l = sizeof(cpio_pattern)-1;
	}

	f = 0;
	for(i=0; i+pattern_l<len; i++) {
	   f = 1;
	   for(j=0; j<pattern_l; j++) {
	      a = p[i+j];
	      if(pattern[j]=='.') {
	         if(a>='A' && a<='F') a = a-'A'+'a';
	         if(!((a>='0' && a<='9') || (a>='a' && a<='f'))) {
	            f = 0;
                    break;
		 }
	      } else if(a!=pattern[j]) {
	         f = 0;
	         break;
	      }
	   }
	   if(f) break;
	}
	if(f) return i;
	return -1;
}

int find_initrd(void *kern, int len, int *initrd_pos, int *initrd_len) {
	char *pkern;
	char *initrd;
	int i_pos;
	int i_len;

	pkern = kern;
	i_pos = find_cpio(pkern, len, 0);
	if(i_pos<0) return 1;
	initrd = &pkern[i_pos];
	i_len = find_cpio(initrd, len-i_pos, 1);
	if(i_len>=0) i_len = (i_len + 0x1ff) & ~0x1ff;
	if(i_len<0 || i_len+i_pos>len) return 1;
	*initrd_pos = i_pos;
	*initrd_len = i_len;
	printf("Found CPIO archive at 0x%06x\n", i_pos);
	return 0;
}

void extract_swap_initrd(int swap, const char *kernel, const char *initrd, const char *output) {
	char buf[4096];
	struct stat kern_stat;
	struct stat initrd_stat;
	int kfd;
	int k_len;
	int i_pos;
	int i_len;
	int ifd;
	int ofd;
	int i;
	int j;
	int n;
	char *k_mmap;
	char *initrd_mmap;
	
	kfd = open(kernel, O_RDONLY);
	if(kfd<0) exit(1);
	fstat(kfd, &kern_stat);
	k_len = kern_stat.st_size;
	k_mmap = mmap(NULL, k_len, PROT_READ, MAP_PRIVATE, kfd, 0);
	
	if(find_initrd(k_mmap, k_len, &i_pos, &i_len)!=0) {
	   fprintf(stderr, "Unable to find initrd in %s\n", kernel);
	   munmap(k_mmap, k_len);
	   close(kfd);
	   exit(1);
	}

	initrd_mmap = &k_mmap[i_pos];

	if(swap) {
	   ifd = open(initrd, O_RDONLY);
	   if(ifd<0) exit(1);
	   fstat(ifd, &initrd_stat);
	   n = initrd_stat.st_size;
	   if(n>i_len) {
	      fprintf(stderr, "Unable to fit the initrd into %d bytes of space\n", i_len);
	      close(ifd);
	      munmap(k_mmap, k_len);
	      close(kfd);
	      exit(1);
	   }
	   ofd = open(output, O_RDWR | O_CREAT, 0644);
	   write(ofd, k_mmap, i_pos);
	   while((i=read(ifd, buf, sizeof(buf)))>0) {
	      write(ofd, buf, i);
	   }
	   for(i=0; i<sizeof(buf); i++) buf[i] = 0;
	   for(i=i_len-n; i>0; i-=j) {
	      if(i>sizeof(buf)) j = write(ofd, buf, sizeof(buf));
	      else j = write(ofd, buf, i);
	   }
	   write(ofd, &initrd_mmap[i_len], k_len-i_pos-i_len);
	   close(ofd);
	   close(ifd);
	   printf("Replaced initrd in %s to %s (%d bytes)\n", output, initrd, n);
	} else {
	   ifd = open(initrd, O_RDWR | O_CREAT, 0644);
	   n = write(ifd, initrd_mmap, i_len);
	   printf("Extracted %d bytes of (possibly) initrd to %s\n", n, initrd);
	   close(ifd);
	}

	munmap(k_mmap, k_len);
	close(kfd);
}

