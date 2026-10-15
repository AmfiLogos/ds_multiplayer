/* Windows tail of rig_sock.c (appended by gen_port.py instead of the AF_UNIX listener):
   TCP listener on 127.0.0.1:<port>; the port is also written to <rig_dir>/ctl.port. */
static char sock_port[16];
static void *accept_thread(void *arg) {
    (void)arg;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)atoi(sock_port));
    int one = 1; setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &one, sizeof one);
    if (bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 8)) { rig_log("control port %s: bind/listen failed (%d)", sock_port, WSAGetLastError()); return NULL; }
    char pp[1100]; snprintf(pp, sizeof pp, "%s/ctl.port", rig_dir);
    FILE *f = fopen(pp, "w"); if (f) { fprintf(f, "%s\n", sock_port); fclose(f); }
    rig_log("control port 127.0.0.1:%s listening", sock_port);
    for (;;) {
        SOCKET c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET) { rig_log("accept failed (%d)", WSAGetLastError()); Sleep(1000); continue; }
        int nd = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
        pthread_t t; pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)c); pthread_detach(t);
    }
    return NULL;
}
void rig_sock_start(const char *port) {
    snprintf(sock_port, sizeof sock_port, "%s", port);
    pthread_t t; pthread_create(&t, NULL, accept_thread, NULL); pthread_detach(t);
}
