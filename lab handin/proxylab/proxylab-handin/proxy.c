
/**
 * Multi-Threaded Proxy Server Program
 * 
 * This program is a multi-threaded proxy server handling HTTP requests and responses. 
 * Key features:
 * - `doit`: Manages HTTP transactions, processing each client request.
 * - `parse_uri`: Extracts host, port, and path from URIs for request routing.
 * - `build_requestheader`: Modifies and forwards HTTP request headers.
 * - `thread`: Operates as worker threads to handle requests concurrently.
 * - `init_cache`: Sets up cache for storing frequently accessed data.
 * - `reader` and `writer`: 
 *      Implement cache access using a reader-writer model for thread safety.
 */
#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include<pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4 
#define SBUFSIZE 16 

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connect_hdr = "Connection: close\r\n";
static const char *proxy_connect_hdr = "Proxy-Connection: close\r\n";

/*
 * reader-writer model
 */
typedef struct {
    char *name;
    char *object;
    int used_cnt;
} CacheLine;

typedef struct {
    int used_cnt;
    CacheLine* objects;
} Cache;
sbuf_t sbuf; // Shared buffer for producer-consumer model.
Cache cache; // Instance of Cache to store the actual cache data.
int readcnt; // Counter to keep track of the number of active readers.
sem_t mutex, w; // Semaphores for synchronization. 
int timen=0;

void doit(int fd);
// Manages HTTP request/response for the client connected via 'fd'.
void parse_uri(char *uri, char *host, char *port, char *path);
// Extracts host, port, and path from the given 'uri'.
void build_requestheader(rio_t *rp, char *newreq, char *method, char *hostname, char *port, char *path);
// Forms a new HTTP request header, storing it in 'newreq'.
void *thread(void* vargp);
// Thread function for handling requests in a multi-threaded environment.
void init_cache();
// Sets up the caching system.
int reader(int fd, char *uri);
// Reads from cache for 'uri', sends data via 'fd' if available.
void writer(char *uri, char *buf,int size);
// Writes 'buf' data to cache under 'uri'.

/**
 * Main function for the proxy server.
 * Sets up signal handling, initializes cache, creates worker threads,
 * and starts listening on the specified port for incoming connections. 
 * Accepts connections and dispatches them to worker threads for processing.
 */
int main(int argc, char **argv) {
    int listenfd, connfd; // Listening and connection file descriptors.
    char hostname[MAXLINE], port[MAXLINE]; // Store client hostname and port.
    socklen_t clientlen; // Length of client address.
    struct sockaddr_storage clientaddr; // Client address.
    pthread_t tid; // Thread identifier.

    // Check command line arguments for port number.
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = open_listenfd(argv[1]); // Open listening socket.
    Signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to handle broken pipe scenarios.
    sbuf_init(&sbuf, SBUFSIZE); // Initialize the buffer.
    init_cache(); // Initialize the cache.

    // Create worker threads.
    for (int i = 0; i < NTHREADS; ++i) {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    // Main loop: accept and handle incoming connections.
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd); // Insert connection descriptor into buffer.
    }

    // This line seems unused and could potentially be removed.
    printf("%s", user_agent_hdr);

    return 0;
}

/* 
 * doit - handle one HTTP request/response transaction.
 * Processes an HTTP request received on the file descriptor 'fd',
 * parses the request, and serves it either from cache or by forwarding it
 * to the intended server.
 */
void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    char new_request[MAXLINE];
    char complete_uri[MAXLINE] = ""; // Initialize to empty string.
    char object_buf[MAX_OBJECT_SIZE];
    rio_t rio_client, rio_server;
    int number, build_server;
    int csize = 0;

    rio_readinitb(&rio_client, fd); // Initialize RIO for client.
    if (!rio_readlineb(&rio_client, buf, MAXLINE))  // Read request line.
        return;

    sscanf(buf, "%s %s %s", method, uri, version); 
    // Parse method, URI, version.
    parse_uri(uri, host, port, path); // Parse URI into host, port, path.

    // Constructing complete URI.
    sprintf(complete_uri, "%s%s", complete_uri, host);
    sprintf(complete_uri, "%s%s", complete_uri, port);
    sprintf(complete_uri, "%s%s", complete_uri, path);

    // Serve from cache if possible.
    if (reader(fd, complete_uri)) {
        fprintf(stdout, "%s from cache\n", uri); // Log cache hit.
        fflush(stdout);
        return;
    }

    build_requestheader(&rio_client, new_request, method, host, port, path); 
    // Build new request header.
    build_server = open_clientfd(host, port); // Connect to the actual server.
    if (build_server < 0) {
        fprintf(stderr, "connect to real server err\n"); 
        // Log error if connection fails.
        return;
    }

    Rio_readinitb(&rio_server, build_server); // Initialize RIO for server.
    Rio_writen(build_server, new_request, strlen(new_request)); 
    // Send the request to the server.

    csize = 0; // Track size of the response.
    while ((number = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0) {
        Rio_writen(fd, buf, number); // Write server response to client.
        if (csize + number > MAX_OBJECT_SIZE) {
            csize = -1; // Mark as too large to cache.
        }
        if (csize != -1) {
            printf("get %d bytes from server\n", number); // Log received bytes.
            memcpy(object_buf + csize, buf, number); // Append to cache buffer.
            csize += number;
        }
    }

    if (csize != -1) {
        writer(complete_uri, object_buf,csize); // Cache the response.
    }

    close(build_server); // Close server connection.
}

/*
 * parse_uri - parse URI into host, port, and path.
 * Extracts the host name, port number, and path from a given URI.
 * Assumes default port 80 if no port is specified.
 * The function modifies the host, port, and path strings in place.
 */
void parse_uri(char *uri, char *host, char *port, char *path) {
    char *hostpos = strstr(uri, "//");
    if (hostpos == NULL) {
        // No protocol specified in URI, check for path.
        char *pathpos = strstr(uri, "/");
        if (pathpos != NULL) {
            // Only path is present in the URI.
            strcpy(path, pathpos);
            strcpy(port, "80"); // Default port to 80 if not specified.
            return;
        }
        // Default to http protocol if not specified.
        hostpos = uri;
    } else {
        hostpos += 2; // Move past the "//".
    }

    char *portpos = strstr(hostpos, ":");
    if (portpos != NULL) {
        // Port is specified in the URI.
        int portnum;
        char *pathpos = strstr(portpos, "/");
        if (pathpos != NULL) {
            sscanf(portpos + 1, "%d%s", &portnum, path);
        } else {
            // No path specified in the URI.
            sscanf(portpos + 1, "%d", &portnum);
        }
        sprintf(port, "%d", portnum);
        *portpos = '\0'; // Terminate the host string at the port specifier.
    } else {
        // No port specified, default to 80.
        char *pathpos = strstr(hostpos, "/");
        if (pathpos != NULL) {
            strcpy(path, pathpos);
            *pathpos = '\0'; // Terminate the host string at the path.
        }
        strcpy(port, "80");
    }

    strcpy(host, hostpos); // Copy the host portion into host variable.
    return;
}

/**
 * Constructs the HTTP request header for the proxy.
 * Filters out certain headers from the original request and adds necessary headers.
 */
void build_requestheader(rio_t *rp, char *newreq, char *method, char *hostname, char *port, char *path) {
    sprintf(newreq, "%s %s HTTP/1.0\r\n", method, path); // Start constructing the new request header.

    char buf[MAXLINE];
    // Read lines from client input.
    while (Rio_readlineb(rp, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break; // End of request headers.

        // Skip headers that will be set by the proxy.
        if (strstr(buf, "Host:") != NULL || strstr(buf, "User-Agent:") != NULL || 
            strstr(buf, "Connection:") != NULL || strstr(buf, "Proxy-Connection:") != NULL) continue;

        sprintf(newreq, "%s%s", newreq, buf); // Add other headers from the client request.
    }

    // Add necessary headers.
    sprintf(newreq, "%sHost: %s:%s\r\n", newreq, hostname, port);
    sprintf(newreq, "%s%s", newreq, user_agent_hdr); // User-Agent header.
    sprintf(newreq, "%s%s", newreq, connect_hdr); // Connection header.
    sprintf(newreq, "%s%s", newreq, proxy_connect_hdr); // Proxy-Connection header.
    sprintf(newreq, "%s\r\n", newreq); // End of headers.
}

/**
 * Worker thread function.
 * Detaches itself and processes requests from clients in a loop.
 */
void *thread(void* ptr){
    Pthread_detach(Pthread_self()); // Detach thread for autonomous cleanup.

    while (1) {
        int connfd = sbuf_remove(&sbuf); // Retrieve a connection descriptor from the buffer.
        doit(connfd); // Handle the HTTP request/response transaction.
        Close(connfd); // Close the connection.
    }
}

/**
 * Initializes the cache.
 * Allocates memory for cache lines and sets up synchronization primitives.
 */
void init_cache() {
    Sem_init(&mutex, 0, 1); // Initialize the mutex semaphore for protecting readcnt.
    Sem_init(&w, 0, 1); // Initialize the writer semaphore for exclusive write access.
    readcnt = 0; // Initialize the reader count.

    // Allocate memory for 10 cache lines.
    cache.objects = (CacheLine*)Malloc(sizeof(CacheLine) * 10);
    cache.used_cnt = 0;
    for (int i = 0; i < 10; i++) {
        cache.objects[i].name = Malloc(sizeof(char) * MAXLINE); // Allocate memory for the name.
        cache.objects[i].object = Malloc(sizeof(char) * MAX_OBJECT_SIZE); // Allocate memory for the object.
        cache.objects[i].used_cnt = 0; // Initialize the used count for LRU tracking.
    }
}

/**
 * Reader function in the reader-writer model.
 * Checks if the requested URI is in the cache and serves it if present.
 */
int reader(int fd, char *uri) {
    int in_cache = 0;

    P(&mutex); // Lock to increment readcnt.
    readcnt++;
    if (readcnt == 1) P(&w); // First reader acquires writer lock.
    V(&mutex); // Unlock after incrementing readcnt.

    // Search the cache for the URI.
    for (int i = 0; i < 10; ++i) {
        if (!strcmp(cache.objects[i].name, uri)) {
            Rio_writen(fd, cache.objects[i].object, MAX_OBJECT_SIZE); // Serve from cache.
            in_cache = 1;
            break;
        }
    }

    P(&mutex); // Lock to decrement readcnt.
    readcnt--;
    if (readcnt == 0) V(&w); // Last reader releases writer lock.
    V(&mutex); // Unlock after decrementing readcnt.

    return in_cache; // Return cache status.
}
/**
 * Writer function in the reader-writer model.
 * Writes or updates cache entries based on LRU (Least Recently Used) policy.
 */
void writer(char *uri, char *buf, int size) {
    P(&w); // Acquire exclusive write access.

    // Implement LRU policy to find the least recently used cache line.
    int least_used_index = 0;
    int least_used_count = cache.objects[0].used_cnt;
    for (int i = 1; i < 10; i++) {
        if (cache.objects[i].used_cnt < least_used_count) {
            least_used_count = cache.objects[i].used_cnt;
            least_used_index = i;
        }
    }

    // Update the used count for LRU tracking.
    cache.objects[least_used_index].used_cnt = ++least_used_count;
    strcpy(cache.objects[least_used_index].name, uri); // Update the name.
    memcpy(cache.objects[least_used_index].object, buf, size); // Copy the object data.

    V(&w); // Release exclusive write access.
}