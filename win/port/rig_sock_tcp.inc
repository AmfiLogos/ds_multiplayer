/* Windows tail of rig_sock.c (appended by gen_port.py instead of the AF_UNIX listener):
   TCP listener on 127.0.0.1:<DSRIG_PORT> (test launchers only); the port is also written to <rig_dir>/ctl.port, the owner pid
   to <rig_dir>/ctl.pid, the connection token to <rig_dir>/ctl.token (channel guard in src/rig_sock.c). */
static char sock_port[16];
static void *accept_thread(void *arg) {
    (void)arg;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((unsigned short)atoi(sock_port));
    int one = 1; setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, &one, sizeof one);
    SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);            /* a child process (soft restart B) never holds it */
    /* soft restart: B starts while A still holds the port; A quits after the swap. Retry until the port is free
       (1 s steps, 15 min at most) instead of giving up, so B takes the same port once A is gone. */
    for (int tries = 0;; tries++) {
        if (!bind(s, (struct sockaddr *)&a, sizeof a)) break;
        int e = WSAGetLastError();
        if ((e != WSAEADDRINUSE && e != WSAEACCES) || tries >= 900) { rig_log("control port %s: bind failed (%d)", sock_port, e); return NULL; }
        if (!tries) rig_log("control port %s: in use (%d), waiting for it", sock_port, e);
        Sleep(1000);
    }
    if (!ctl_token_write()) { closesocket(s); return NULL; }   /* after bind (a soft restart's B owns the port now), before listen */
    if (listen(s, 8)) { rig_log("control port %s: listen failed (%d)", sock_port, WSAGetLastError()); return NULL; }
    char pp[1100]; snprintf(pp, sizeof pp, "%s/ctl.pid", rig_dir);   /* which process owns the port (ctl.port stays the bare number) */
    FILE *f = fopen(pp, "w"); if (f) { fprintf(f, "%lu\n", (unsigned long)GetCurrentProcessId()); fclose(f); }
    snprintf(pp, sizeof pp, "%s/ctl.port", rig_dir);
    f = fopen(pp, "w"); if (f) { fprintf(f, "%s\n", sock_port); fclose(f); }
    rig_log("control port 127.0.0.1:%s listening (pid %lu)", sock_port, (unsigned long)GetCurrentProcessId());
    for (;;) {
        SOCKET c = accept(s, NULL, NULL);
        if (c == INVALID_SOCKET) { rig_log("accept failed (%d)", WSAGetLastError()); Sleep(1000); continue; }
        int nd = 1; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
        pthread_t t; pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)c); pthread_detach(t);
    }
    return NULL;
}
/* Test tools only: listens only when DSRIG_PORT is set (by the test launchers, never by a player's game); no default port.
   DSRIG_NO_CTL=1: off as on the Mac. */
void rig_sock_start(const char *port) {
    const char *no = getenv("DSRIG_NO_CTL");
    if (no && *no == '1') { rig_log("control socket off (DSRIG_NO_CTL=1)"); return; }
    if (!port || !*port) { rig_log("control socket off (no DSRIG_PORT)"); return; }
    snprintf(sock_port, sizeof sock_port, "%s", port);
    pthread_t t; pthread_create(&t, NULL, accept_thread, NULL); pthread_detach(t);
}
