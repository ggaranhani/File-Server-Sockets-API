#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_CLIENTS         10
#define MAX_REQ_LEN         1000
#define FBLOCK_SIZE         4000
#define REQ_GET             "GET"
#define REQ_APPEND          "APPEND"
#define REQ_REMOVE          "REMOVE"
#define REQ_CREATE          "CREATE"
#define COD_OK0_GET         "OK-0 method GET OK\n"
#define COD_OK1_FILE        "OK-1 file opened OK\n"
#define COD_OK2_CREATE      "OK-2 method CREATE OK\n"
#define COD_OK3_FILE        "OK-3 file created OK\n"
#define COD_OK4_REMOVE      "OK-4 method REMOVE OK\n"
#define COD_OK5_FILE        "OK-5 file removed OK\n"
#define COD_OK6_APPEND      "OK-6 method APPEND OK\n"
#define COD_OK7_FILE        "OK-7 file append OK\n"
#define COD_ERROR_0_METHOD  "ERROR-0 method not supported\n"
#define COD_ERROR_1_FILE    "ERROR-1 file could not be opened\n"
#define COD_ERROR_2_CREATE  "ERROR-2 could not create file\n"
#define COD_ERROR_5_REMOVE  "ERROR-5 could not remove file\n"
#define COD_ERROR_4_APPEND  "ERROR-4 could not append to file\n"

struct req_t {
    char method[128];
    char data[FBLOCK_SIZE];
    char filename[256];
};
typedef struct req_t req_t;

struct sockaddr_in caddr[MAX_CLIENTS];
int     sl,
        addr_len[MAX_CLIENTS],
        sc[MAX_CLIENTS],
        nr[MAX_CLIENTS];
char    request[MAX_CLIENTS][MAX_REQ_LEN];
req_t   req[MAX_CLIENTS];

struct sockaddr_in saddr;

char *get_first_word(char *str) {
    const char sp = ' ';
    char *firstsp = strchr(str, sp);
    int i, firstlen = strlen(str)-strlen(firstsp);
    char *first = malloc((firstlen)*sizeof(char));

    for (i = 0; i < firstlen; i++) {
        first[i] = str[i];
    }
    return first;
}

void get_request(req_t *r, char *rstr) {
    const char sp = ' ';
    char *fword = get_first_word(rstr);
    char *lword = strrchr(rstr, sp); 
    if(strcmp(fword, REQ_APPEND) == 0){
        rstr = strchr(rstr, sp);
        int datasize = strlen(rstr)-strlen(lword);
        bzero(r, sizeof(req_t));
        strcpy(r->filename, lword+1);
        strncpy(r->data, rstr, datasize*sizeof(char));
        strcpy(r->method, fword);
    } else {
        bzero(r, sizeof(req_t));
        sscanf(rstr, "%s %s", r->method, r->filename);
    }
}

void send_file(int sockfd, req_t r) {
    int fd, nr;
    unsigned char fbuff[4000];
    fd = open(r.filename, O_RDONLY, S_IRUSR);
    if (fd == -1) {
        perror("open");
        send(sockfd, COD_ERROR_1_FILE, strlen(COD_ERROR_1_FILE), 0);
        return;
    }
    send(sockfd, COD_OK1_FILE, strlen(COD_OK1_FILE), 0);

    do{
        bzero(fbuff, FBLOCK_SIZE);  
        nr = read(fd, fbuff, FBLOCK_SIZE);
        if (nr > 0) {
            send(sockfd, fbuff, nr, 0);
        }
    } while(nr > 0);

    close(fd);
    return;
}

void apend_file(int sockfd, req_t r) {
    FILE *f = fopen(r.filename, "a");
    if (f == NULL) {
        perror("fopen");
        send(sockfd, COD_ERROR_1_FILE, strlen(COD_ERROR_1_FILE), 0);
        return;
    }
    send(sockfd, COD_OK1_FILE, strlen(COD_OK1_FILE), 0);

    fprintf(f, "%s", r.data);

    fclose(f);
    return;
}

void create_file(int sockfd, req_t r) {
    FILE *f = fopen(r.filename, "w");
    if (f == NULL) {
        perror("fopen");
        send(sockfd, COD_ERROR_2_CREATE, strlen(COD_ERROR_2_CREATE), 0);
        return;
    }
    send(sockfd, COD_OK3_FILE, strlen(COD_OK3_FILE), 0);
    fclose(f);
}

void remove_file(int sockfd, req_t r) {
    int rem = remove(r.filename);
    if (rem == 0) {
        send(sockfd, COD_OK5_FILE, strlen(COD_OK5_FILE), 0);
    } else {
        perror("remove");
        send(sockfd, COD_ERROR_5_REMOVE, sizeof(COD_ERROR_5_REMOVE), 0);
    }
    return;
}

void proc_request(int sockfd, req_t r) {
    if (strcmp(r.method, REQ_GET) == 0) {
        send(sockfd, COD_OK0_GET, strlen(COD_OK0_GET), 0);
        send_file(sockfd, r);
    } else if (strcmp(r.method, REQ_CREATE) == 0) {
        send(sockfd, COD_OK2_CREATE, strlen(COD_OK2_CREATE), 0);
        create_file(sockfd, r);
    } else if (strcmp(r.method, REQ_REMOVE) == 0) {
        send(sockfd, COD_OK4_REMOVE, strlen(COD_OK4_REMOVE), 0);
        remove_file(sockfd, r);
    } else if (strcmp(r.method, REQ_APPEND) == 0) {
        send(sockfd, COD_OK6_APPEND, strlen(COD_OK6_APPEND), 0);
        apend_file(sockfd, r);
    } else {
        send(sockfd, COD_ERROR_0_METHOD, strlen(COD_ERROR_0_METHOD), 0);
    }
    return;
}


void *server(void *i) {
    int sid = (long)i;
    printf("s_id: %d\n", sid);
    while (1) {
        addr_len[sid] = sizeof(struct sockaddr_in);

        sc[sid] = accept(sl, (struct sockaddr *)&caddr[sid], (socklen_t *)&addr_len[sid]);
        if (sc[sid] == -1) {
            perror("accept");
            continue;
        }
        printf("Conectado com cliente %s:%d\n", 
            inet_ntoa(caddr[sid].sin_addr), 
            ntohs(caddr[sid].sin_port)
        );

        bzero(request[sid], MAX_REQ_LEN);
        
        nr[sid] = recv(sc[sid], request[sid], MAX_REQ_LEN, 0);
        if (nr > 0) {
            get_request(&req[sid], request[sid]);
            printf("method: %s\n filename: %s\n", req[sid].method, req[sid].filename);
            proc_request(sc[sid], req[sid]);
        }
        close(sc[sid]);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
    printf("Uso: %s <porta>\n", argv[0]);
        return 0;
    }
    long i;
    
    sl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sl ==-1) {
        perror("socket");
        return 0;
    }
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(atoi(argv[1]));
    
    if (bind(sl, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) == -1) {
        perror("bind");
        return 0;
    }
    //mil clientes podem aguardar conexao
    if (listen(sl, 1000) == -1) {
        perror("listen");
        return 0;
    }
    
    pthread_t thread[MAX_CLIENTS];

    for (i = 0; i < MAX_CLIENTS; i++) {
        pthread_create(&thread[i], NULL, server, (void *)i);
    }
    for (i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(thread[i], NULL);
    }

    close(sl);

    return 0;
}