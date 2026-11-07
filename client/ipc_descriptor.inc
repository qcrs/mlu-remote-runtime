int mlu_remote_ipc_export_fd(mlu_remote_context *ctx,uint64_t token) {
  if(!ctx)return -1;
  struct sockaddr_storage peer;socklen_t size=sizeof(peer);
  if(getpeername(ctx->fd,(struct sockaddr *)&peer,&size))return -1;
  int fd=socket(peer.ss_family,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -1;
  int64_t deadline=mlu_deadline_after(ctx->rpc_timeout_ms);int st=0;
  if(connect(fd,(struct sockaddr *)&peer,size)) {
    int error=0;socklen_t n=sizeof(error);
    if(errno!=EINPROGRESS || mlu_wait_socket(fd,POLLOUT,deadline) || getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&n) || error)st=-1;
  }
  struct mlu_remote_wire_header h={htonl(MLU_REMOTE_MAGIC),htons(MLU_REMOTE_VERSION),htons(MLU_OP_IPC_ATTACH),htonll_u(1),htonl(8),0};
  uint64_t wire=htonll_u(token),received=0;
  if(!st && (mlu_io_full(fd,&h,sizeof(h),1,deadline)!=1 || mlu_io_full(fd,&wire,8,1,deadline)!=1))st=-1;
  if(!st){
    for(;;){ssize_t n=recv(fd,&received,8,MSG_PEEK);if(n==8)break;if(!n || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) || mlu_wait_socket(fd,POLLIN,deadline)){st=-1;break;}}
    if(received!=wire)st=-1;
  }
  if(st){close(fd);return -1;}return fd;
}
int mlu_remote_ipc_fd_token(int fd,uint64_t *token) {
  /* Duplicate so a concurrent close cannot invalidate this operation. */
  int held=fcntl(fd,F_DUPFD_CLOEXEC,0);if(held<0)return -1;
  uint64_t wire=0;ssize_t n=recv(held,&wire,8,MSG_PEEK|MSG_DONTWAIT);close(held);
  if(n!=8)return -1;*token=ntohll_u(wire);return 0;
}
