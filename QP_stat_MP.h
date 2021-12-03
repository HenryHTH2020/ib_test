
#define _GNU_SOURCE

#include <pthread.h>
#include <byteswap.h>
#include <endian.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <malloc.h>
#include <semaphore.h>
#include <fcntl.h>    /* For O_* constants */
#include <sys/stat.h> /* For mode constants */
#include <string.h>
#ifndef QP_STAT
#define QP_STAT

//#define NOPRINT

/*     \033[显示方式;前景色;背景色m
	  显示方式 :0（默认值）、1（高亮）、22（非粗体）、4（下划线）、24（非下划线）、5（闪烁）、25（非闪烁）、7（反显）、27（非反显）
	 前景色:30（黑色）、31（红色）、32（绿色）、 33（黄色）、34（蓝色）、35（洋红）、36（青色) 、37（白色）
	 背景色:40（黑色）、41（红色）、42（绿色）、 43（黄色）、44（蓝色）、45（洋红）、46（青色）、47（白色）
	\033[0m 默认
	\033[1;32;40m 绿色
	033[1;31;40m 红色 */
#define PRINT_FOR_ROOT(msg...)                                  \
    do                                                          \
    {                                                           \
        if (!proc_index)                                        \
        {                                                       \
            printf("\033[1;31m");                               \
            printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                        \
            printf("\033[0m");                                  \
        }                                                      \
    } while (0)

#define PRINT_IN_RED(msg...)                                \
    do                                                      \
    {                                                       \
        printf("\033[1;31m");                               \
        printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
        printf(msg);                                        \
        printf("\033[0m");                                  \
    } while (0)

#ifndef NOPRINT
#define PRINT_IN_GREEN(msg...)                                  \
    do                                                          \
    {                                                           \
        if (log_verbose_level >= 4)                             \
        {                                                       \
            printf("\033[1;32m");                               \
            printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                        \
            printf("\033[0m");                                  \
        }                                                       \
    } while (0)

#define PRINT_IN_BLUE(msg...)                                   \
    do                                                          \
    {                                                           \
        if (log_verbose_level >= 3)                             \
        {                                                       \
            printf("\033[1;36m");                               \
            printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                        \
            printf("\033[0m");                                  \
        }                                                       \
    } while (0)

#define PRINT_IN_PINK(msg...)                                   \
    do                                                          \
    {                                                           \
        if (log_verbose_level >= 1)                             \
        {                                                       \
            printf("\033[1;35m");                               \
            printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                        \
            printf("\033[0m");                                  \
        }                                                       \
    } while (0)

#define PRINT_IN_YELLOW(msg...)                                 \
    do                                                          \
    {                                                           \
        if (log_verbose_level >= 2)                             \
        {                                                       \
            printf("\033[1;33m");                               \
            printf("pid: %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                        \
            printf("\033[0m");                                  \
        }                                                       \
    } while (0)

#else

#define PRINT_IN_GREEN(msg...)
#define PRINT_IN_BLUE(msg...)
#define PRINT_IN_PINK(msg...)
#define PRINT_IN_YELLOW(msg...)

#endif
//#define RDMA_CHECK(expr, msg, place) do { int res = (expr); printf(msg); goto place;} while(0)

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll(uint64_t x)
{
    return bswap_64(x);
}
static inline uint64_t ntohll(uint64_t x) { return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll(uint64_t x) { return x; }
static inline uint64_t ntohll(uint64_t x) { return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

#define PRINT(msg...)                                             \
    do                                                            \
    {                                                             \
        if (log_verbose_level >= 1)                               \
        {                                                         \
            printf("pid:\t %d\tp_ind%d\t", getpid(), proc_index); \
            printf(msg);                                          \
        }                                                         \
    } while (0)

// result is in usec
size_t get_timestamp();

struct testparams
{
    int opcode;
    int qp_type;

    int inline_mode;
    int pingpong_mode;
    int log_verbose_level;
    char VersionID[50];
    char test_subject[20];

    size_t REPEAT;
    size_t TIMEOUT;
    size_t TRY_TIMEOUT;
    size_t LIMIT;

    unsigned int qp_quantity;
    unsigned int wr_per_qp;
    size_t mr_size;
    size_t msg_size;
    int path_mtu;
    int thread_bound;

    char page_size_str[10];
};
struct shmem_struct
{
    struct testparams params;
    size_t pingpong_count[100];
    size_t msg_post_quantity[100];
    size_t recved_quantity[100];
    int ready;
    int start;
    uint32_t error_flag;
    double avrg_post_clk[100];
    double avrg_post_to_poll_clk[100];

};

/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t
{
    uint64_t *addr;   /* MR start address */
    uint32_t *rkey;   /* Remote key 00*/
    uint32_t *qp_num; /* QP number */
    uint16_t lid;     /* LID of the IB port */
    uint8_t gid[16];  /* gid */
} __attribute__((packed));

struct config_t
{
    const char *dev_name;    /* IB device name */
    char *server_name;       /* server host name */
    u_int32_t tcp_data_port; /* server TCP port */
    u_int32_t tcp_ctrl_port; /* server TCP port */
    int ib_port;             /* local IB port to work with */
    int gid_idx;             /* gid index to use */
};

struct resources
{
    struct ibv_device_attr device_attr;
    /* Device attributes */
    struct ibv_port_attr port_attr;    /* IB port attributes */
    struct cm_con_data_t remote_props; /* values to connect to remote side */
    struct ibv_context *ib_ctx;        /* device handle */
    struct ibv_context ** ib_ctx_array;
    struct ibv_pd *pd;                 /* PD handle */
    struct ibv_cq *cq;                 /* CQ handle */
    struct ibv_qp **qp;                /* QP handle */
    struct ibv_mr **mr_array;          /* MR handle for buf */
    char *buf;                         /* memory buffer pointer, used for RDMA and send
ops */
    char **buf_arr;
    int data_sock; /* TCP socket file descriptor */
    int ctrl_sock;
    struct ibv_ah *ah;
};

extern uint32_t error_flag;
extern int end_this_loop;

extern int ok;
extern int test_mode;
extern struct resources res;

extern int use_data_gram;
extern size_t test_time_length;
extern int qp_type;
extern int opcode;
extern int log_verbose_level;

extern struct config_t config;
extern int port_offset;

extern char VersionID[50];
extern char test_subject[20];

extern size_t REPEAT;
extern size_t TIMEOUT;
extern size_t TRY_TIMEOUT;
extern size_t MAX_THREAD_QUANTITY;
extern size_t LIMIT;

extern int proc_index;
extern int proc_quantity;

extern size_t qp_quantity_array[];
extern size_t mr_per_qp_array[];
extern size_t msg_size_array[];

extern int qp_quantity_index;
extern int mr_per_qp_index;
extern int msg_size_index;
extern size_t thread_quantity;
extern size_t dev_ctx;
extern unsigned int qp_quantity;
extern unsigned int mr_per_qp;
extern unsigned int mr_quantity;
extern unsigned int wr_per_qp;
extern int share_mr_between_qp;
extern int share_buf_between_mr;
extern size_t buf_qtt;
extern double avrg_post_clk, avrg_post_to_poll_clk;

extern size_t mr_size;
extern size_t msg_size;
extern int path_mtu;
extern int thread_bound;

extern char time_string[40];
extern struct ibv_send_wr *sr_list;
extern struct ibv_recv_wr *rr_list;
extern struct ibv_sge *send_sge_list;
extern struct ibv_sge *recv_sge_list;

extern struct ibv_sge *sge_list;
extern char page_size_str[10];
extern size_t page_size;
extern size_t total_mmap_size;

extern char *special_prefix_QPC;

extern int pingpong_mode;
extern int inline_mode;
extern int max_inline_length;
extern const int DEVICE_INLINE_LENGTH;

extern size_t received_quantity;

extern int recv_id_offset;

extern const int DEVICE_INLINE_LENGTH;

extern int param_shmem_fd;

extern char *sem_name_test_attr;
extern char *sem_name_end;
extern char *sem_name_test_start;
extern char *sem_name_test_ready;
extern char *sem_name_shmem_access;
extern char *sem_name_ready_2;
extern char *sem_name_start_2;

//share memory
extern struct shmem_struct *pshemem_struct;
extern const int init_start_flag;
char sync_confirm_index;

//polling

extern int global_max_poll_quantity;
extern int unsignaled;

int sock_connect(const char *servername, int port);

int sock_create(struct resources *res);

int open_dev(struct resources *res);

int alloc_pd(struct resources *res);

int create_cq(struct resources *res);

int alloc_buf(struct resources *res);

int alloc_mr(struct resources *res);

int create_qp(struct resources *res);

/* exchange data */
int sock_sync(int sock, int xfer_size, char *local_data,
              char *remote_data);
int sock_send(int sock, int xfer_size, char *local_data);
int sock_receive(int sock, int xfer_size, char *remote_data);

int modify_qp_to_init(struct ibv_qp *qp);

int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn,
                     uint16_t dlid, uint8_t *dgid);

int modify_qp_to_rts(struct ibv_qp *qp);
void create_ah_attr_for_UD();

void free_condata_attributes(struct cm_con_data_t *con_data);

int connect_qp(struct resources *res);
int connect_qp_ud(struct resources *res);

int resources_create(struct resources *res);
int resources_destroy(struct resources *res);

int prepare_wrs(struct resources *res);

int post_send(struct resources *res, int i);
int try_post_send(struct resources *res, int i);
int try_multi_post_send(struct resources *res,int qp_index, struct ibv_send_wr * sl);
int post_recv(struct resources *res, int i);
int multi_post_recv(struct resources *res, int qp_index, struct ibv_recv_wr * rl);

int poll_completion(struct resources *res, unsigned long timeout, int *i);
int poll_completion_with_imm(struct resources *res, unsigned long timeout, int *i, int *imm);


int try_multi_poll_completion(struct resources *res, unsigned long timeout, int max_poll_qtt, struct ibv_wc * wc_list);
int try_poll_completion_with_imm(struct resources *res, unsigned long timeout, int *i, int *imm);
int try_poll_completion(struct resources *res, unsigned long timeout, int *i);

void print_result(size_t tv);

int destroy_sock(struct resources *res);

void print_info();

void prt_rst_to_file(size_t tv);
void prt_rst_to_file_fixed_time(size_t xfer_quantity, int summary);

void print_final_result(size_t tv);
void print_final_result_fixed_time_length();

void alloc_buffer_huge_page(struct resources *res);

size_t test_core(struct resources *res);

size_t test_bw(struct resources *res);
size_t test_lat(struct resources *res);
size_t test_msg_rate(struct resources *res);
size_t test_msg_rate_ud(struct resources *res);
size_t test_msg_rate_QPC(struct resources *res);
size_t test_core_pingpong(struct resources *res);
size_t test_bw_pingpong_timelength(struct resources *res);

size_t test_lat_pingpong(struct resources *res);

void set_repeat();

int confirm_mem_req();

void setCPUbound(int number_bound);

void checkCPUbound(int number_bound);

void set_global_attr();

int test_body(void);

void config_inline();

void set_recv_id_offset();

int check_QP_state();
void print_pingpong_count();
void *create_shared_memory();
void *open_shared_memory();

sem_t *clear_and_open_sem(char *sem_name);

sem_t *sem_open_existing(char *sem_name);
void barrier_sem_inter_intra_nodes();
void barrier_sem_intra_nodes();

void barrier_sem(sem_t *sem1, sem_t *sem2);

int write_uniform_params();
void set_verID_subj_ps(char *verid, char *sub, char *psstr);
static void usage(const char *argv0);
inline static uint64_t rdtsc();

#endif
