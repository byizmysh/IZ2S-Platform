/* Restrict only this test process and its children. No host network changes. */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#define DENY(n) BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,(n),0,1),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM)
int main(int argc,char**argv){
 if(argc<2)return 2;
 struct sock_filter rules[]={
  BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
  DENY(__NR_socket),DENY(__NR_socketpair),DENY(__NR_connect),
  DENY(__NR_bind),DENY(__NR_listen),DENY(__NR_accept),DENY(__NR_accept4),
  DENY(__NR_sendto),DENY(__NR_recvfrom),DENY(__NR_sendmsg),DENY(__NR_recvmsg),
  DENY(__NR_sendmmsg),DENY(__NR_recvmmsg),
  BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)
 };
 struct sock_fprog program={.len=sizeof rules/sizeof rules[0],.filter=rules};
 if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program)){perror("network restriction");return 3;}
 if(socket(AF_INET,SOCK_STREAM,0)!=-1||errno!=EPERM){fputs("Network restriction was not enforced\n",stderr);return 4;}
 fputs("Network sockets denied for this test and its children\n",stderr);
 execvp(argv[1],argv+1);perror("test execution");return 5;
}
