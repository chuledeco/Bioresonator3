#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#define MAX_CHUNK_SIZE 1024  // Maximum chunk size for file transfer

extern size_t received_samples;

void ws_init();

#endif // WEB_SERVER_H