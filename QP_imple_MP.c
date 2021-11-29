#include "QP_stat_MP.h"

static int bound_offset = 0;

struct resources res;

uint32_t error_flag = 0;
int ok = 0;
int use_data_gram = 0;
int test_mode = 0;
size_t test_time_length = 10000000;
char time_string[40] = "";
const int DEVICE_INLINE_LENGTH_UD = 956; //956
const int DEVICE_INLINE_LENGTH = 828;	 //828
static size_t *mr_pingpong_count = NULL;

size_t page_size;
size_t total_mmap_size;
int recv_id_offset;

int proc_index = 0;
int proc_quantity = 1;

struct ibv_send_wr *sr_list = NULL;
struct ibv_recv_wr *rr_list = NULL;
struct ibv_sge *send_sge_list = NULL;
struct ibv_sge *recv_sge_list = NULL;

int opcode;
int qp_type;

int qp_quantity_index = 0;
int mr_per_qp_index = 0;
int msg_size_index = 0;

int port_offset = 0;

int inline_mode = 1;
int pingpong_mode = 1;
int max_inline_length = 0;
int log_verbose_level = 3;

char VersionID[50] = "QP_MR_V1_test_1";
char test_subject[20] = "msg_rate";
char page_size_str[10] = "4KB";

size_t REPEAT = 10000;
size_t TIMEOUT = 10000;
size_t TRY_TIMEOUT = 5000;
size_t MAX_THREAD_QUANTITY = 1;
size_t LIMIT = 22;

size_t thread_quantity = 1;
unsigned int qp_quantity = 1;
unsigned int mr_per_qp = 1;
unsigned int wr_per_qp = 1;

unsigned int mr_quantity = 1;
int share_mr_between_qp = 0;

size_t mr_size = 1024 * 1024;
size_t msg_size = 1024 * 1024;
int path_mtu = 5;
int thread_bound = 1;

size_t received_quantity = 0;

//shared memory
int param_shmem_fd;
struct shmem_struct *pshemem_struct;

char *sem_name_test_attr = "sem_name_test_attr";
char *sem_name_end = "sem_name_end";
char *sem_name_test_start = "sem_name_test_start";
char *sem_name_test_ready = "sem_name_test_ready";
char *sem_name_shmem_access = "sem_name_shmem_access";
char *sem_name_ready_2 = "sem_name_ready_2";
char *sem_name_start_2 = "sem_name_start_2";

struct config_t config = {NULL,	 /* dev_name */
						  NULL,	 /* server_name */
						  19800, /* tcp_data_port */
						  20000, /* tcp_ctrl_port */
						  1,	 /* ib_port */
						  0 /* gid_idx */};

//polling
int global_max_poll_quantity;
int unsignaled = 0;
static size_t recv_qtt_tmp;
static int barrier_tag = 0;
static size_t old_fashioned_lat = 0;

const int init_start_flag = 0;
static int target_start_flag = 1;

char sync_confirm_index = 0;
// result is in usec
size_t get_timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

int sock_connect(const char *servername, int port)
{
	struct addrinfo *resolved_addr = NULL;
	struct addrinfo *iterator;
	char service[6];
	int sockfd = -1;
	int listenfd = 0;
	struct addrinfo hints = {
		.ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	if (sprintf(service, "%d", port) < 0)
		goto sock_connect_exit;
	/* Resolve DNS address, use sockfd as temp storage */
	sockfd = getaddrinfo(servername, service, &hints, &resolved_addr);
	if (sockfd < 0)
	{
		PRINT_IN_RED("%s for %s:%d\n", gai_strerror(sockfd), servername, port);
		goto sock_connect_exit;
	}
	/* Search through results and find the one we want */
	for (iterator = resolved_addr; iterator; iterator = iterator->ai_next)
	{
		sockfd = socket(iterator->ai_family, iterator->ai_socktype,
						iterator->ai_protocol);
		if (sockfd >= 0)
		{
			if (servername)
			{
				int reuse = 1;
				setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
				/* Client mode. Initiate connection to remote */
				while (connect(sockfd, iterator->ai_addr, iterator->ai_addrlen))
				{
					usleep(10 * 1000); // 10ms
									   // PRINT("failed connect \n");
									   // close(sockfd);
									   // sockfd = -1;
				}
			}
			else
			{
				/* Server mode. Set up listening socket an accept a connection */
				listenfd = sockfd;
				sockfd = -1;
				int reuse = 1;
				setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
				if (bind(listenfd, iterator->ai_addr, iterator->ai_addrlen))
				{
					printf("bind exit\n");
					goto sock_connect_exit;
				}
				listen(listenfd, 1);
				sockfd = accept(listenfd, NULL, 0);
			}
		}
	}
sock_connect_exit:
	if (listenfd)
		close(listenfd);
	if (resolved_addr)
		freeaddrinfo(resolved_addr);
	if (sockfd < 0)
	{
		if (servername)
		{
			PRINT_IN_RED("Couldn't connect to %s:%d\n", servername, port);
			exit(1);
		}
		else
		{
			perror("server accept");
			PRINT_IN_RED("accept() failed\n");
			exit(1);
		}
	}
	return sockfd;
}

int sock_create(struct resources *res)
{
	int rc = 0;
	/* if client side */
	if (config.server_name)
	{
		res->data_sock = sock_connect(config.server_name, config.tcp_data_port);
		if (res->data_sock < 0)
		{
			PRINT_IN_RED("failed to establish TCP connection to server %s, port %d\n",
						 config.server_name, config.tcp_data_port);
			rc = -1;
			exit(1);
		}
	}
	else
	{
		PRINT("waiting on port %d for TCP connection\n", config.tcp_data_port);
		res->data_sock = sock_connect(NULL, config.tcp_data_port);
		if (res->data_sock < 0)
		{
			PRINT_IN_RED("failed to establish TCP connection with client on port %d\n",
						 config.tcp_data_port);
			rc = -1;
			exit(1);
		}
	}
	// PRINT("TCP data connection was established\n");
	if (config.server_name)
	{
		res->ctrl_sock = sock_connect(config.server_name, config.tcp_ctrl_port);
		if (res->ctrl_sock < 0)
		{
			PRINT_IN_RED("failed to establish TCP connection to server %s, port %d\n",
						 config.server_name, config.tcp_ctrl_port);
			rc = -1;
			exit(1);
		}
	}
	else
	{
		PRINT("waiting on port %d for TCP connection\n", config.tcp_ctrl_port);
		res->ctrl_sock = sock_connect(NULL, config.tcp_ctrl_port);
		if (res->ctrl_sock < 0)
		{
			PRINT_IN_RED("failed to establish TCP connection with client on port %d\n",
						 config.tcp_ctrl_port);
			rc = -1;
			exit(1);
		}
	}
	return rc;
}

int open_dev(struct resources *res)
{
	int rc;
	struct ibv_device **dev_list = NULL;
	struct ibv_device *ib_dev = NULL;
	int num_devices;
	dev_list = ibv_get_device_list(&num_devices);
	for (int i = 0; i < num_devices; i++)
	{
		PRINT_IN_BLUE("%s\n", ibv_get_device_name(dev_list[i]));
		if (!strcmp(ibv_get_device_name(dev_list[i]), config.dev_name))
		{
			ib_dev = dev_list[i];
			break;
		}
	}
	if (!ib_dev)
	{
		PRINT_IN_RED("get dev error");
		exit(1);
	}
	res->ib_ctx = ibv_open_device(ib_dev);
	if (!res->ib_ctx)
	{
		PRINT_IN_RED("open dev error");
		exit(1);
	}
	ibv_free_device_list(dev_list);
	dev_list = NULL;
	ib_dev = NULL;
	if (ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr))
	{
		PRINT_IN_RED("query error");
		exit(1);
	}

	return rc;
}

int alloc_pd(struct resources *res)
{
	res->pd = ibv_alloc_pd(res->ib_ctx);
	if (!res->pd)
	{
		PRINT_IN_RED("alloc pd error");
		exit(1);
	}
	return 0;
}

int create_cq(struct resources *res)
{

	int cq_size = qp_quantity * (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp) + 1;
	if (pingpong_mode)
	{
		cq_size *= 2;
	}
	res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
	if (!res->cq)
	{
		PRINT_IN_RED("create cq error");
		exit(1);
	}
	return 0;
}

int alloc_buf_old(struct resources *res)
{

	if (!strcmp(page_size_str, "4KB"))
	{
		size_t buf_size;
		buf_size = mr_size * mr_quantity;

		if (pingpong_mode)
		{
			buf_size *= 2;
		}
		res->buf = (char *)malloc(buf_size);
		if (!res->buf)
		{
			perror("buffer alloc");
			PRINT_IN_RED("alloc buf error");
			exit(1);
		}
		memset(res->buf, 0, buf_size);

		PRINT_IN_BLUE("buf_addr:%p\nbuf_size: %lu\n", res->buf, buf_size);
	}
	else
	{
		alloc_buffer_huge_page(res);
	}
	return 0;
}

int alloc_buf(struct resources *res)
{

	if (!strcmp(page_size_str, "4KB"))
	{

		size_t buf_qtt = mr_quantity;
		if (pingpong_mode)
		{
			buf_qtt *= 2;
		}
		res->buf_arr = (char **)memalign(page_size, sizeof(char *) * buf_qtt);
		for (int i = 0; i < buf_qtt; i++)
		{
			res->buf_arr[i] = (char *)memalign(page_size, mr_size);
			if (!res->buf_arr[i])
			{
				perror("buffer alloc");
				PRINT_IN_RED("alloc buf error");
				exit(1);
			}
			PRINT_IN_BLUE("buf[%d]addr:%p\n", i, res->buf_arr[i]);
			memset(res->buf_arr[i], 0, mr_size);
		}
	}
	else
	{
		alloc_buffer_huge_page(res);
	}
	return 0;
}
int alloc_mr(struct resources *res)
{
	int mr_flags;
	int total_mr_quantity = mr_quantity;
	if (pingpong_mode)
	{
		total_mr_quantity *= 2;
	}
	mr_flags =
		IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
	if (qp_type == IBV_QPT_UD)
	{
		mr_flags = IBV_ACCESS_LOCAL_WRITE;
	}
	res->mr_array = (struct ibv_mr **)malloc(sizeof(struct ibv_mr *) * total_mr_quantity);

	PRINT_IN_YELLOW("total mr quantity is %d\n", total_mr_quantity);

	for (int i = 0; i < total_mr_quantity; i++)
	{
		PRINT_IN_BLUE("allocating mr [%d]\n", i);
		if (res->buf)
		{
			res->mr_array[i] = ibv_reg_mr(res->pd, res->buf + mr_size * i, mr_size, mr_flags);
		}
		else if (res->buf_arr)
		{
			res->mr_array[i] = ibv_reg_mr(res->pd, res->buf_arr[i], mr_size, mr_flags);
		}
		if (!res->mr_array[i])
		{
			PRINT_IN_RED("alloc mr_array[%d] error\n", i);
			exit(1);
		}
		PRINT_IN_BLUE("MR[%d] was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
					  i, res->mr_array[i]->addr, res->mr_array[i]->lkey, res->mr_array[i]->rkey, mr_flags);
	}
	return 0;
}

int create_qp(struct resources *res)
{

	struct ibv_qp_init_attr qp_init_attr;
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.qp_type = qp_type;
	if (qp_type == IBV_QPT_UD)
	{

		qp_init_attr.sq_sig_all = 1;

		qp_init_attr.send_cq = res->cq;
		qp_init_attr.recv_cq = res->cq;
		qp_init_attr.cap.max_send_wr = (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp) + 1;
		qp_init_attr.cap.max_recv_wr = (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp) + 1;

		qp_init_attr.cap.max_send_sge = 1;
		qp_init_attr.cap.max_recv_sge = 1;
		qp_init_attr.cap.max_inline_data = max_inline_length;

		res->qp = (struct ibv_qp **)malloc(sizeof(struct ibv_qp *) * qp_quantity);

		for (int i = 0; i < qp_quantity; i++)
		{
			res->qp[i] = ibv_create_qp(res->pd, &qp_init_attr);
			if (!res->qp)
			{
				PRINT_IN_RED("create qp error\n");
				exit(1);
			}
			struct ibv_qp_attr attr;
			struct ibv_qp_init_attr init_attr;
			ibv_query_qp(res->qp[i], &attr, IBV_QP_CAP, &init_attr);
			//PRINT_IN_RED("my max_inline_length %d; max inline data %u\n", max_inline_length, init_attr.cap.max_inline_data);
		}
	}
	else if (qp_type == IBV_QPT_RC || qp_type == IBV_QPT_UC)
	{

		qp_init_attr.sq_sig_all = 1;
		qp_init_attr.send_cq = res->cq;
		qp_init_attr.recv_cq = res->cq;
		qp_init_attr.cap.max_send_wr = mr_per_qp + 1;
		qp_init_attr.cap.max_recv_wr = mr_per_qp + 1;
		qp_init_attr.cap.max_send_sge = 1;
		qp_init_attr.cap.max_recv_sge = 1;
		qp_init_attr.cap.max_inline_data = max_inline_length;

		res->qp = (struct ibv_qp **)malloc(sizeof(struct ibv_qp *) * qp_quantity);
		for (int i = 0; i < qp_quantity; i++)
		{
			res->qp[i] = ibv_create_qp(res->pd, &qp_init_attr);

			if (!res->qp)
			{
				PRINT_IN_RED("create qp error");
				exit(1);
			}
		}
	}
	return 0;
}

/* exchange data */
int sock_sync(int sock, int xfer_size, char *local_data,
			  char *remote_data)
{
	int xfer_unit = 8192;
	int xfer_time = xfer_size / xfer_unit;
	for (int i = 0; i < xfer_time; i++)
	{
		int read_bytes = 0;
		int written_bytes = 0;
		int total_read_bytes = 0;
		int total_written_bytes = 0;
		PRINT_IN_YELLOW("before write data\n");

		while (total_written_bytes < xfer_unit)
		{
			written_bytes = write(sock, local_data + xfer_unit * i + total_written_bytes, xfer_unit);
			if (written_bytes)
			{
				PRINT_IN_BLUE("%d bytes are writen \n", written_bytes);
			}
			if (written_bytes >= 0)
			{
				total_written_bytes += written_bytes;
			}
			else
			{
				PRINT_IN_RED("Failed writing data during sock_sync_data\n");
				return 1;
			}
		}
		while (total_read_bytes < xfer_unit)
		{

			read_bytes = read(sock, remote_data + i * xfer_unit + total_read_bytes, xfer_unit - total_read_bytes);
			if (read_bytes)
			{
				PRINT_IN_BLUE("%d bytes are read\n", read_bytes);
			}
			if (read_bytes >= 0)
				total_read_bytes += read_bytes;
			else
			{
				PRINT_IN_RED("Failed reading data during sock_sync_data\n");
				return 1;
			}
		}
	}
	int remainder = xfer_size % xfer_unit;
	if (remainder)
	{
		int read_bytes = 0;
		int total_read_bytes = 0;
		int written_bytes = 0;
		int total_written_bytes = 0;
		PRINT_IN_YELLOW("before write data\n");

		while (total_written_bytes < remainder)
		{
			written_bytes = write(sock, local_data + xfer_unit * xfer_time + total_written_bytes, remainder);
			if (written_bytes)
			{
				PRINT_IN_BLUE("%d bytes are writen \n", written_bytes);
			}
			if (written_bytes >= 0)
			{
				total_written_bytes += written_bytes;
			}
			else
			{
				PRINT_IN_RED("Failed writing data during sock_sync_data\n");
				return 1;
			}
		}

		while (total_read_bytes < remainder)
		{

			read_bytes = read(sock, remote_data + xfer_time * xfer_unit + total_read_bytes, remainder - total_read_bytes);
			if (read_bytes)
			{
				PRINT_IN_BLUE("%d bytes are read\n", read_bytes);
			}
			if (read_bytes >= 0)
				total_read_bytes += read_bytes;
			else
			{
				PRINT_IN_RED("Failed reading data during sock_sync_data\n");
				return 1;
			}
		}
	}
	return 0;
}
int sock_send(int sock, int xfer_size, char *local_data)
{
	int rc;
	int read_bytes = 0;
	int total_read_bytes = 0;
	rc = write(sock, local_data, xfer_size);
	PRINT_IN_BLUE("%d bytes are writen \n", rc);
	uint8_t *t = local_data;
	if (rc < xfer_size)
	{
		PRINT_IN_RED("Failed writing data during sock_sync_data\n");
		exit(1);
	}
	else
		rc = 0;
	return rc;
}

int sock_receive(int sock, int xfer_size, char *remote_data)
{
	int rc;
	int read_bytes = 0;
	int total_read_bytes = 0;
	int flag = 1;
	while (!rc && total_read_bytes < xfer_size)
	{
		read_bytes = read(sock, remote_data, xfer_size);
		if (read_bytes && flag)
		{
			PRINT_IN_BLUE("%d bytes are read\n", read_bytes);
		}
		if (read_bytes > 0)
			total_read_bytes += read_bytes;
		else
			rc = read_bytes;
	}
	return rc;
}
int modify_qp_to_init(struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.port_num = config.ib_port;
	attr.pkey_index = 0;
	if (qp_type == IBV_QPT_RC || qp_type == IBV_QPT_UC)
	{
		attr.qp_access_flags =
			IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
		flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
	}
	else if (qp_type == IBV_QPT_UD)
	{
		attr.qkey = 0x11111111;
		flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
	}
	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc)
	{
		PRINT_IN_RED("Error in ibv_modify_qp(init)\n");
		exit(1);
	}
	return rc;
}

int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t remote_qpn,
					 uint16_t dlid, uint8_t *dgid)
{
	struct ibv_qp_attr attr;
	int flags;
	if (qp_type == IBV_QPT_RC)
	{
		memset(&attr, 0, sizeof(attr));
		attr.qp_state = IBV_QPS_RTR;
		attr.path_mtu = path_mtu;
		attr.dest_qp_num = remote_qpn;
		attr.rq_psn = 0;
		attr.max_dest_rd_atomic = 1;
		attr.min_rnr_timer = 0x12;
		attr.ah_attr.is_global = 0;
		attr.ah_attr.dlid = dlid;
		attr.ah_attr.sl = 0;
		attr.ah_attr.src_path_bits = 0;
		attr.ah_attr.port_num = config.ib_port;
		if (config.gid_idx > 0)
		{
			attr.ah_attr.is_global = 1;
			attr.ah_attr.port_num = 1;
			memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
			attr.ah_attr.grh.flow_label = 0;
			attr.ah_attr.grh.hop_limit = 1;
			attr.ah_attr.grh.sgid_index = config.gid_idx;
			attr.ah_attr.grh.traffic_class = 0;
		}
		flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
				IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
	}
	else if (qp_type == IBV_QPT_UC)
	{
		memset(&attr, 0, sizeof(attr));
		attr.qp_state = IBV_QPS_RTR;
		attr.path_mtu = path_mtu;
		attr.dest_qp_num = remote_qpn;
		attr.rq_psn = 0;

		attr.ah_attr.is_global = 0;
		attr.ah_attr.dlid = dlid;
		attr.ah_attr.sl = 0;
		attr.ah_attr.src_path_bits = 0;
		attr.ah_attr.port_num = config.ib_port;
		config.gid_idx;

		if (config.gid_idx >= 0)
		{
			attr.ah_attr.is_global = 1;
			attr.ah_attr.port_num = 1;
			memcpy(&attr.ah_attr.grh.dgid, dgid, 16);
			attr.ah_attr.grh.flow_label = 0;
			attr.ah_attr.grh.hop_limit = 1;
			attr.ah_attr.grh.sgid_index = config.gid_idx;
			attr.ah_attr.grh.traffic_class = 0;
		}
		flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
				IBV_QP_RQ_PSN;
	}
	else if (qp_type == IBV_QPT_UD)
	{
		memset(&attr, 0, sizeof(attr));
		attr.qp_state = IBV_QPS_RTR;

		flags = IBV_QP_STATE;
	}
	else
	{
		PRINT_IN_RED("qp type error\n");
		exit(1);
	}
	if (ibv_modify_qp(qp, &attr, flags))
	{
		PRINT_IN_RED("Error in ibv_modify_qp(rtr)\n");
		exit(1);
	}
	return 0;
}

int modify_qp_to_rts(struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	if (qp_type == IBV_QPT_RC)
	{
		attr.timeout = 0x12;
		attr.retry_cnt = 6;
		attr.rnr_retry = 6;
		attr.sq_psn = 0;
		attr.max_rd_atomic = 1;
		flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
				IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
	}
	else if (qp_type == IBV_QPT_UC)
	{

		attr.sq_psn = 0;
		flags = IBV_QP_STATE | IBV_QP_SQ_PSN;
	}
	else if (qp_type == IBV_QPT_UD)
	{
		attr.sq_psn = 0;
		flags = IBV_QP_STATE | IBV_QP_SQ_PSN;
	}
	else
	{
		PRINT_IN_RED("qp type error\n");
		exit(1);
	}
	if (ibv_modify_qp(qp, &attr, flags))
	{
		PRINT_IN_RED("Error in ibv_modify_qp(rts)\n");
		exit(1);
	}
	return 0;
}

void create_ah_attr_for_UD()
{
	if (qp_type != IBV_QPT_UD)
	{
		return;
	}
	struct ibv_ah_attr ah_attr = {
		.is_global = 0,
		.dlid = res.remote_props.lid,
		.sl = 0,
		.src_path_bits = 0,
		.port_num = config.ib_port};

	if (((union ibv_gid *)(res.remote_props.gid))->global.interface_id)
	{
		PRINT_IN_PINK("In ah attr for ud is global set to 1\n");
		ah_attr.is_global = 1;
		ah_attr.grh.hop_limit = 1;
		//ah_attr.grh.dgid = res.remote_props.gid;
		memcpy(&ah_attr.grh.dgid, res.remote_props.gid, 16);
		ah_attr.grh.sgid_index = config.gid_idx;
	}
	else
	{
		PRINT_IN_PINK("In ah attr for ud is global remains 0\n");
	}

	res.ah = ibv_create_ah(res.pd, &ah_attr);
	if (!res.ah)
	{
		PRINT_IN_RED("Failed to create AH\n");
		exit(1);
		return;
	}
}

void init_condata_attributes(struct cm_con_data_t *con_data)
{
	con_data->addr = (uint64_t *)malloc(sizeof(uint64_t) * mr_quantity);
	con_data->rkey = (uint32_t *)malloc(sizeof(uint32_t) * mr_quantity);
	con_data->qp_num = (uint32_t *)malloc(sizeof(uint32_t) * qp_quantity);
}
void free_condata_attributes(struct cm_con_data_t *con_data)
{
	PRINT_IN_YELLOW("FREEING con data\n");
	if (con_data->addr)
	{
		free(con_data->addr);
		con_data->addr = NULL;
	}
	if (con_data->rkey)
	{
		free(con_data->rkey);
		con_data->rkey = NULL;
	}
	if (con_data->qp_num)
	{
		free(con_data->qp_num);
		con_data->qp_num = NULL;
	}
}

int connect_qp(struct resources *res)
{

	struct con_data_qpn
	{
		uint32_t qp_num[qp_quantity];
	} __attribute__((packed));

	struct con_data_rkey_addr
	{
		uint32_t rkey[mr_quantity];
		uint64_t addr[mr_quantity];
	} __attribute__((packed));

	struct cm_con_data_t local_con_data;
	init_condata_attributes(&local_con_data);

	struct cm_con_data_t remote_con_data;
	init_condata_attributes(&remote_con_data);
	struct cm_con_data_t tmp_con_data;

	union ibv_gid my_gid;
	int rc;
	memset(&my_gid, 0, sizeof my_gid);
	if (rc = ibv_query_gid(res->ib_ctx, config.ib_port, config.gid_idx, &my_gid))
	{
		PRINT_IN_RED("error when getting gid for port %d, index %d\n", config.ib_port, config.gid_idx);
		exit(1);
	}
	else
	{
		// PRINT_IN_YELLOW("got gid for port %d, index %d, mygidptr %p\n", config.ib_port, config.gid_idx, &my_gid);
		memcpy(local_con_data.gid, &my_gid, 16);
		uint8_t *p = local_con_data.gid;
		PRINT_IN_PINK("local GID "
					  "=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
					  "%02x:%02x:%02x\n",
					  p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10],
					  p[11], p[12], p[13], p[14], p[15]);
	}
	/* exchange using TCP sockets info required to connect QPs */

	local_con_data.lid = htons(res->port_attr.lid);
	PRINT_IN_PINK("local LID = 0x%x\n", res->port_attr.lid);
	if (rc = sock_sync(res->data_sock, sizeof(struct cm_con_data_t),
					   (char *)&local_con_data, (char *)&tmp_con_data))
	{
		PRINT_IN_RED("failed to exchange connection data between sides\n");
		exit(1);
	}
	else
	{
		PRINT_IN_YELLOW("successfully exchanged connection data between sides\n");
		// PRINT_IN_YELLOW("local condata pointer:%p, temp condata pointer:%p\n", &local_con_data, &tmp_con_data);
	}

	init_condata_attributes(&tmp_con_data);

	struct con_data_qpn cd_qpn;
	struct con_data_qpn tmp_cd_qpn;
	memset(&cd_qpn, 0, sizeof(cd_qpn));
	memset(&tmp_cd_qpn, 0, sizeof(tmp_cd_qpn));

	for (int i = 0; i < qp_quantity; i++)
	{

		cd_qpn.qp_num[i] = htonl(res->qp[i]->qp_num);
		//local_con_data.qp_num[i] = htonl(res->qp[i]->qp_num);

		PRINT_IN_BLUE("local QP number[%d] = 0x%x\n", i, res->qp[i]->qp_num);
	}

	if (rc = sock_sync(res->data_sock, sizeof(struct con_data_qpn),
					   (char *)&cd_qpn, (char *)&tmp_cd_qpn))
	{
		PRINT_IN_RED("failed to exchange qp_num data between sides\n");
		exit(1);
	}

	PRINT_IN_YELLOW("successfully exchanged qp_num data between sides\n");

	for (int i = 0; i < qp_quantity; i++)
	{

		remote_con_data.qp_num[i] = ntohl(tmp_cd_qpn.qp_num[i]);
		PRINT_IN_BLUE("Remote QP number[%d] = 0x%x\tpointer:%p\n", i, remote_con_data.qp_num[i], &remote_con_data.qp_num[i]);
	}

	PRINT_IN_PINK("after qp assignment\n");
	if (opcode != IBV_WR_SEND_WITH_IMM && opcode != IBV_WR_SEND)
	{

		struct con_data_rkey_addr cd_rkadd;
		struct con_data_rkey_addr tmp_cd_rkadd;
		PRINT_IN_YELLOW("before memset \n");
		memset(&cd_rkadd, 0, sizeof(cd_rkadd));
		memset(&tmp_cd_rkadd, 0, sizeof(tmp_cd_rkadd));
		PRINT_IN_YELLOW("after memset \n");
		if (pingpong_mode)
		{
			PRINT_IN_YELLOW("in pingpong mode\n");
			for (int i = 0; i < mr_quantity; i++)
			{
				cd_rkadd.addr[i] = htonll((uintptr_t)res->mr_array[i + mr_quantity]->addr);
				cd_rkadd.rkey[i] = htonl(res->mr_array[i + mr_quantity]->rkey);

				PRINT_IN_BLUE("local address[%d] = 0x%" PRIx64 "\n", i, (uint64_t)(res->mr_array[i + mr_quantity]->addr));
				PRINT_IN_BLUE("local rkey[%d] = 0x%x\n", i, res->mr_array[i + mr_quantity]->rkey);
			}
		}
		else
		{
			for (int i = 0; i < mr_quantity; i++)
			{
				cd_rkadd.addr[i] = htonll((uintptr_t)res->mr_array[i]->addr);
				cd_rkadd.rkey[i] = htonl(res->mr_array[i]->rkey);

				PRINT_IN_BLUE("local address[%d] = 0x%" PRIx64 "\n", i, (uint64_t)(res->mr_array[i]->addr));
				PRINT_IN_BLUE("local rkey[%d] = 0x%x\n", i, res->mr_array[i]->rkey);
			}
		}
		PRINT_IN_YELLOW("before addrk sync\n");
		if (rc = sock_sync(res->data_sock, sizeof(struct con_data_rkey_addr),
						   (char *)&cd_rkadd, (char *)&tmp_cd_rkadd))
		{
			PRINT_IN_RED("failed to exchange rkey and addr data between sides\n");
			exit(1);
		}
		else
		{
			PRINT_IN_YELLOW("successfully exchanged rkey and addr data between sides\n");
			// PRINT_IN_YELLOW("local condata pointer:%p, temp condata pointer:%p\n", &local_con_data, &tmp_con_data);
		}
		PRINT_IN_YELLOW("after addrk sync\n");

		for (int i = 0; i < mr_quantity; i++)
		{
			remote_con_data.addr[i] = ntohll(tmp_cd_rkadd.addr[i]);
			remote_con_data.rkey[i] = ntohl(tmp_cd_rkadd.rkey[i]);

			PRINT_IN_BLUE("Remote address[%d] = 0x%" PRIx64 "\n", i, remote_con_data.addr[i]);
			PRINT_IN_BLUE("Remote rkey[%d] = 0x%x\n", i, remote_con_data.rkey[i]);
		}
	}
	remote_con_data.lid = ntohs(tmp_con_data.lid);
	memcpy(remote_con_data.gid, tmp_con_data.gid, 16);
	/* save the remote side attributes, we will need it for the post SR */
	res->remote_props = remote_con_data;

	PRINT_IN_PINK("Remote LID = 0x%x\n", remote_con_data.lid);
	if (config.gid_idx >= 0)
	{
		uint8_t *p = remote_con_data.gid;
		PRINT_IN_PINK("Remote GID "
					  "=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
					  "%02x:%02x:%02x\n",
					  p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10],
					  p[11], p[12], p[13], p[14], p[15]);
	}
	if (qp_type == IBV_QPT_UD)
	{
		create_ah_attr_for_UD();
	}
	for (int i = 0; i < qp_quantity; i++)
	{
		modify_qp_to_init(res->qp[i]);
		modify_qp_to_rtr(res->qp[i], remote_con_data.qp_num[i], remote_con_data.lid, remote_con_data.gid);
		modify_qp_to_rts(res->qp[i]);
	}

	char tempchar;
	if (rc = sock_sync(res->ctrl_sock, 1, "Q", &tempchar))
	{
		PRINT_IN_RED("error in sync after QP to RTS\n");
		exit(1);
	}
	else
	{
		PRINT_IN_YELLOW("successful sync after QP to RTS\n");
	}
	free_condata_attributes(&local_con_data);
	free_condata_attributes(&tmp_con_data);
	return 0;
}

int resources_destroy(struct resources *res)
{
	int rc = 0;

	//log_verbose_level += 4;
	if (res->qp)
	{
		PRINT_IN_YELLOW("destroy QP\n");
		for (int i = 0; i < qp_quantity; i++)
		{
			if (rc = ibv_destroy_qp(res->qp[i]))
			{
				PRINT_IN_RED("error in destroying QP[%d] with errno %d\n", i, rc);
			}
			else
			{
				PRINT_IN_GREEN("QP[%d] destroyed\n", i);
			}
			res->qp[i] = NULL;
		}
		PRINT_IN_YELLOW("FREEing QP\n");
		free(res->qp);
		res->qp = NULL;
	}
	//log_verbose_level -= 4;
	PRINT_IN_YELLOW("destroy mr array\n");
	if (res->mr_array)
	{
		int total_mr_qtt = mr_quantity;
		if (pingpong_mode)
		{
			total_mr_qtt *= 2;
		}
		for (int i = 0; i < total_mr_qtt; i++)
		{
			PRINT_IN_PINK("dereg mr %d\n", i);
			ibv_dereg_mr(res->mr_array[i]);
			res->mr_array[i] = NULL;
		}
		PRINT_IN_YELLOW("FREEing mr array\n");
		free(res->mr_array);
		res->mr_array = NULL;
	}

	PRINT_IN_YELLOW("destroy condata\n");
	free_condata_attributes(&res->remote_props);
	PRINT_IN_YELLOW("destroy buf\n");

	if (res->buf)
	{
		PRINT_IN_YELLOW("FREEing BUF\n");

		free(res->buf);
		res->buf = NULL;
	}
	//free_buf(res);

	if (res->buf_arr)
	{
		int total_mr_qtt = mr_quantity;
		if (pingpong_mode)
		{
			total_mr_qtt *= 2;
		}
		for (int i = 0; i < total_mr_qtt; i++)
		{
			PRINT_IN_YELLOW("FREEING BUFF_ARR[%d]\n", i);
			if (res->buf_arr[i])
			{
				free(res->buf_arr[i]);
				res->buf_arr[i] = NULL;
			}
		}
		free(res->buf_arr);
		res->buf_arr = NULL;
	}
	PRINT_IN_YELLOW("destroy cq\n");
	if (res->cq)
	{
		ibv_destroy_cq(res->cq);
		res->cq = NULL;
	}
	PRINT_IN_YELLOW("destroy pd\n");

	if (res->pd)
	{
		ibv_dealloc_pd(res->pd);
		res->pd = NULL;
	}
	PRINT_IN_YELLOW("destroy ib_ctx\n");

	if (res->ib_ctx)
	{
		ibv_close_device(res->ib_ctx);
		res->ib_ctx = NULL;
	}

	if (sr_list)
	{
		free(sr_list);
		sr_list = NULL;
	}
	if (rr_list)
	{
		free(rr_list);
		rr_list = NULL;
	}
	if (send_sge_list)
	{
		free(send_sge_list);
		send_sge_list = NULL;
	}
	if (recv_sge_list)
	{
		free(recv_sge_list);
		recv_sge_list = NULL;
	}
	if (mr_pingpong_count)
	{
		free(mr_pingpong_count);
		mr_pingpong_count = NULL;
	}
	PRINT_IN_YELLOW("src destroyed\n");
	return rc;
}

int prepare_wrs(struct resources *res)
{
	unsigned int inline_flag = 0;
	if (max_inline_length)
	{
		inline_flag = IBV_SEND_INLINE;
	}
	if (pingpong_mode)
	{ // same wrs on both sides
		if (!strcmp(test_subject, "msg_rate") && qp_type == IBV_QPT_UC && opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		{
			int err = 0;
			int wr_index = 0;
			sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			// recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);

			memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			// memset(recv_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
			{
				send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
				send_sge_list[wr_index].length = msg_size;
				send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
				// prepare the send work request
				sr_list[wr_index].next = NULL;
				sr_list[wr_index].wr_id = wr_index;
				sr_list[wr_index].sg_list = &send_sge_list[wr_index];
				sr_list[wr_index].num_sge = 1;

				sr_list[wr_index].opcode = opcode;
				sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
				sr_list[wr_index].imm_data = wr_index;
				if (!msg_size)
				{
					sr_list[wr_index].sg_list = NULL;
					sr_list[wr_index].num_sge = 0;
					sr_list[wr_index].send_flags &= ~inline_flag;
				}
				//sr_list[wr_index].send_flags &= ~IBV_SEND_SIGNALED;
				if (msg_size && opcode != IBV_WR_SEND && opcode != IBV_WR_SEND_WITH_IMM)
				{
					sr_list[wr_index].wr.rdma.remote_addr = res->remote_props.addr[(wr_index) % (qp_quantity * mr_per_qp)];
					sr_list[wr_index].wr.rdma.rkey = res->remote_props.rkey[(wr_index) % (qp_quantity * mr_per_qp)];
				}
				// prepare the recv work request
				rr_list[wr_index].next = NULL;
				rr_list[wr_index].wr_id = wr_index + qp_quantity * mr_per_qp;
				rr_list[wr_index].sg_list = NULL;
				rr_list[wr_index].num_sge = 0;
			}
		}
		else if (qp_type == IBV_QPT_RC && opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		{
			// for both sides
			int err = 0;
			int wr_index = 0;
			sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
			{
				send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
				send_sge_list[wr_index].length = msg_size;
				send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
				/* prepare the send work request */
				if ((wr_index + 1) % mr_per_qp)
				{
					sr_list[wr_index].next = &sr_list[wr_index + 1];
				}
				else
				{
					sr_list[wr_index].next = NULL;
				}
				sr_list[wr_index].wr_id = wr_index;
				sr_list[wr_index].sg_list = &send_sge_list[wr_index];
				sr_list[wr_index].num_sge = 1;
				sr_list[wr_index].opcode = opcode;
				sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
				sr_list[wr_index].imm_data = wr_index;
				//PRINT_IN_PINK("srlist [%d], wrid %lu, imm %u\n", wr_index, sr_list[wr_index].wr_id, sr_list[wr_index].imm_data);
				if (opcode != IBV_WR_SEND)
				{
					sr_list[wr_index].wr.rdma.remote_addr = res->remote_props.addr[(wr_index) % (qp_quantity * mr_per_qp)];
					sr_list[wr_index].wr.rdma.rkey = res->remote_props.rkey[(wr_index) % (qp_quantity * mr_per_qp)];
				}
				// prepare the recv work request
				if ((wr_index + 1) % mr_per_qp)
				{
					rr_list[wr_index].next = &rr_list[wr_index + 1];
				}
				else
				{
					rr_list[wr_index].next = NULL;
				}
				rr_list[wr_index].wr_id = wr_index + recv_id_offset;
				rr_list[wr_index].sg_list = NULL;
				rr_list[wr_index].num_sge = 0;
			}
		}
		else if (qp_type == IBV_QPT_RC && opcode == IBV_WR_SEND_WITH_IMM)
		{
			// for both sides
			int err = 0;
			int wr_index = 0;
			sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			memset(recv_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
			{
				send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
				send_sge_list[wr_index].length = msg_size;
				send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
				/* prepare the send work request */
				sr_list[wr_index].next = NULL;
				sr_list[wr_index].wr_id = wr_index;
				sr_list[wr_index].sg_list = &send_sge_list[wr_index];
				sr_list[wr_index].num_sge = 1;
				sr_list[wr_index].opcode = opcode;
				sr_list[wr_index].send_flags = inline_flag;
				sr_list[wr_index].imm_data = wr_index;

				// prepare the recv work request
				recv_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index + qp_quantity * mr_per_qp]->addr);
				recv_sge_list[wr_index].length = msg_size;
				recv_sge_list[wr_index].lkey = res->mr_array[wr_index + qp_quantity * mr_per_qp]->lkey;
				rr_list[wr_index].next = NULL;
				rr_list[wr_index].wr_id = wr_index + recv_id_offset;
				rr_list[wr_index].sg_list = &recv_sge_list[wr_index];
				rr_list[wr_index].num_sge = 1;
			}
		}
		else if (qp_type == IBV_QPT_UC && opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		{
			if (config.server_name)
			{
				int err = 0;
				int wr_index = 0;
				sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
				{
					send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
					send_sge_list[wr_index].length = msg_size;
					send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
					/* prepare the send work request */
					sr_list[wr_index].next = NULL;
					sr_list[wr_index].wr_id = wr_index;
					sr_list[wr_index].sg_list = &send_sge_list[wr_index];
					sr_list[wr_index].num_sge = 1;
					sr_list[wr_index].opcode = opcode;
					sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
					sr_list[wr_index].imm_data = wr_index;
					if (opcode != IBV_WR_SEND)
					{
						sr_list[wr_index].wr.rdma.remote_addr = res->remote_props.addr[wr_index];
						sr_list[wr_index].wr.rdma.rkey = res->remote_props.rkey[wr_index];
					}
				}
			}
		}
		else if (qp_type == IBV_QPT_UD && opcode == IBV_WR_SEND_WITH_IMM)
		{
			PRINT_IN_PINK("preparing WRS FOR UD SEND WITH IMM\n");
			int wr_index = 0;
			sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
			memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
			memset(recv_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
			for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
			{

				send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr + 40);
				send_sge_list[wr_index].length = msg_size;
				send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;

				/* prepare the send work request */
				if ((wr_index + 1) % mr_per_qp)
				{
					sr_list[wr_index].next = &sr_list[wr_index + 1];
				}
				else
				{
					sr_list[wr_index].next = NULL;
				}
				sr_list[wr_index].wr_id = wr_index;
				sr_list[wr_index].sg_list = &send_sge_list[wr_index];
				sr_list[wr_index].num_sge = 1;

				sr_list[wr_index].opcode = opcode;
				sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
				sr_list[wr_index].imm_data = wr_index;
				sr_list[wr_index].wr.ud.ah = res->ah;
				sr_list[wr_index].wr.ud.remote_qpn = res->remote_props.qp_num[wr_index / mr_per_qp];
				sr_list[wr_index].wr.ud.remote_qkey = 0x11111111;
				if (!msg_size)
				{
					sr_list[wr_index].sg_list = NULL;
					sr_list[wr_index].num_sge = 0;
					sr_list[wr_index].send_flags &= ~inline_flag;
				}
				//sr_list[wr_index].send_flags =0;
				PRINT_IN_YELLOW("wr[%d] remote QPN %u, remoteQPK %u\n", wr_index, sr_list[wr_index].wr.ud.remote_qpn, sr_list[wr_index].wr.ud.remote_qkey);
				//recv part
				recv_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index + qp_quantity * mr_per_qp]->addr);
				recv_sge_list[wr_index].length = mr_size;
				recv_sge_list[wr_index].lkey = res->mr_array[wr_index + qp_quantity * mr_per_qp]->lkey;

				if ((wr_index + 1) % mr_per_qp)
				{
					rr_list[wr_index].next = &rr_list[wr_index + 1];
				}
				else
				{
					rr_list[wr_index].next = NULL;
				}
				rr_list[wr_index].wr_id = wr_index + qp_quantity * mr_per_qp;
				rr_list[wr_index].sg_list = &recv_sge_list[wr_index];
				rr_list[wr_index].num_sge = 1;
				if (msg_size == 0)
				{
					rr_list[wr_index].num_sge = 0;
					rr_list[wr_index].sg_list = NULL;
				}
				PRINT_IN_PINK("lkey: %8x\n", rr_list[wr_index].sg_list->lkey);
			}
		}
		else
		{
			PRINT_IN_RED("configuration invalid during prepare wr\n");
			exit(1);
		}
	}
	else // not pingpong
	{
		char *special_prefix = "kangning_1";
		if (config.server_name) // client side
		{
			if (qp_type == IBV_QPT_RC)
			{
				int err = 0;
				int wr_index = 0;
				sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
				{
					send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
					send_sge_list[wr_index].length = msg_size;
					send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
					/* prepare the send work request */
					sr_list[wr_index].next = NULL;
					sr_list[wr_index].wr_id = wr_index;
					sr_list[wr_index].sg_list = &send_sge_list[wr_index];
					sr_list[wr_index].num_sge = 1;
					sr_list[wr_index].opcode = opcode;
					sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
					if (opcode != IBV_WR_SEND)
					{
						sr_list[wr_index].wr.rdma.remote_addr = res->remote_props.addr[wr_index];
						sr_list[wr_index].wr.rdma.rkey = res->remote_props.rkey[wr_index];
					}
				}
			}
			else if (qp_type == IBV_QPT_UC && opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
			{
				int wr_index = 0;
				sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
				memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
				for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
				{

					send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
					send_sge_list[wr_index].length = msg_size;
					send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
					/* prepare the send work request */
					sr_list[wr_index].next = NULL;
					sr_list[wr_index].wr_id = wr_index;
					sr_list[wr_index].sg_list = &send_sge_list[wr_index];
					sr_list[wr_index].num_sge = 1;

					sr_list[wr_index].opcode = opcode;
					sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
					sr_list[wr_index].imm_data = wr_index;

					if (!msg_size)
					{
						sr_list[wr_index].sg_list = NULL;
						sr_list[wr_index].num_sge = 0;
						//sr_list[wr_index].send_flags &= ~inline_flag;
					}
					//sr_list[wr_index].send_flags =0;

					if (msg_size && opcode != IBV_WR_SEND && opcode != IBV_WR_SEND_WITH_IMM)
					{
						sr_list[wr_index].wr.rdma.remote_addr = res->remote_props.addr[wr_index];
						sr_list[wr_index].wr.rdma.rkey = res->remote_props.rkey[wr_index];
					}
				}
			}
			else if (qp_type == IBV_QPT_UD && opcode == IBV_WR_SEND_WITH_IMM)
			{
				if (mr_per_qp == 1 && wr_per_qp >= 1 && share_mr_between_qp)
				{
					int wr_index = 0;
					sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * wr_per_qp);
					send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge));
					memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * wr_per_qp);
					memset(send_sge_list, 0, sizeof(struct ibv_sge));
					send_sge_list[0].addr = (uintptr_t)(res->mr_array[0]->addr + 40);
					send_sge_list[0].length = msg_size;
					send_sge_list[0].lkey = res->mr_array[0]->lkey;
					for (wr_index = 0; wr_index < wr_per_qp * qp_quantity; wr_index++)
					{
						/* prepare the send work request */
						if ((wr_index + 1) % wr_per_qp)
						{
							sr_list[wr_index].next = &sr_list[wr_index + 1];
						}
						else
						{
							sr_list[wr_index].next = NULL;
						}
						sr_list[wr_index].wr_id = wr_index;
						sr_list[wr_index].sg_list = &send_sge_list[0];
						sr_list[wr_index].num_sge = 1;

						sr_list[wr_index].opcode = opcode;
						sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;

						sr_list[wr_index].imm_data = wr_index;
						sr_list[wr_index].wr.ud.ah = res->ah;
						sr_list[wr_index].wr.ud.remote_qpn = res->remote_props.qp_num[wr_index / wr_per_qp];
						sr_list[wr_index].wr.ud.remote_qkey = 0x11111111;
						if (!msg_size)
						{
							sr_list[wr_index].sg_list = NULL;
							sr_list[wr_index].num_sge = 0;
							sr_list[wr_index].send_flags &= ~inline_flag;
						}
					}
				}
				else
				{
					PRINT_IN_PINK("preparing WRS FOR UD SEND WITH IMM\n");
					int wr_index = 0;
					sr_list = (struct ibv_send_wr *)malloc(sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
					send_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
					memset(sr_list, 0, sizeof(struct ibv_send_wr) * qp_quantity * mr_per_qp);
					memset(send_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
					//if (strlen(VersionID) >= strlen(special_prefix) && !memcmp(VersionID, special_prefix, strlen(special_prefix)) && share_mr_between_qp)
					if (share_mr_between_qp)
					{

						for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
						{
							if (wr_index < mr_per_qp)
							{
								send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr + 40);
								send_sge_list[wr_index].length = msg_size;
								send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
							}
							/* prepare the send work request */
							if ((wr_index + 1) % mr_per_qp)
							{
								sr_list[wr_index].next = &sr_list[wr_index + 1];
							}
							else
							{
								sr_list[wr_index].next = NULL;
							}
							sr_list[wr_index].wr_id = wr_index;
							sr_list[wr_index].sg_list = &send_sge_list[wr_index % mr_per_qp];
							sr_list[wr_index].num_sge = 1;

							sr_list[wr_index].opcode = opcode;
							sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
							sr_list[wr_index].imm_data = wr_index;
							sr_list[wr_index].wr.ud.ah = res->ah;
							sr_list[wr_index].wr.ud.remote_qpn = res->remote_props.qp_num[wr_index / mr_per_qp];
							sr_list[wr_index].wr.ud.remote_qkey = 0x11111111;
							if (!msg_size)
							{
								sr_list[wr_index].sg_list = NULL;
								sr_list[wr_index].num_sge = 0;
								sr_list[wr_index].send_flags &= ~inline_flag;
							}
							//sr_list[wr_index].send_flags =0;
							//PRINT_IN_YELLOW("wr[%d] remote QPN %u, remoteQPK %u\n", wr_index, sr_list[wr_index].wr.ud.remote_qpn, sr_list[wr_index].wr.ud.remote_qkey);
						}

						PRINT_IN_RED("same\n");
					}
					else
					{
						for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
						{
							send_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr + 40);
							send_sge_list[wr_index].length = msg_size;
							send_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
							/* prepare the send work request */
							if ((wr_index + 1) % mr_per_qp)
							{
								sr_list[wr_index].next = &sr_list[wr_index + 1];
							}
							else
							{
								sr_list[wr_index].next = NULL;
							}
							sr_list[wr_index].wr_id = wr_index;
							sr_list[wr_index].sg_list = &send_sge_list[wr_index];
							sr_list[wr_index].num_sge = 1;

							sr_list[wr_index].opcode = opcode;
							sr_list[wr_index].send_flags = IBV_SEND_SIGNALED | inline_flag;
							sr_list[wr_index].imm_data = wr_index;
							sr_list[wr_index].wr.ud.ah = res->ah;
							sr_list[wr_index].wr.ud.remote_qpn = res->remote_props.qp_num[wr_index / mr_per_qp];
							sr_list[wr_index].wr.ud.remote_qkey = 0x11111111;
							if (!msg_size)
							{
								sr_list[wr_index].sg_list = NULL;
								sr_list[wr_index].num_sge = 0;
								sr_list[wr_index].send_flags &= ~inline_flag;
							}
							//sr_list[wr_index].send_flags =0;
							PRINT_IN_PINK("wr[%d] remote QPN %u, remoteQPK %u\n", wr_index, sr_list[wr_index].wr.ud.remote_qpn, sr_list[wr_index].wr.ud.remote_qkey);
							sr_list[wr_index];
						}
					}

					{
						struct ibv_port_attr port_info = {};
						int mtu;
						if (ibv_query_port(res->ib_ctx, config.ib_port, &port_info))
						{
							PRINT_IN_RED("Unable to query port info for port %d\n", config.ib_port);
							exit(1);
						}
						mtu = 1 << (port_info.active_mtu + 7);
						if (msg_size > mtu)
						{
							PRINT_IN_RED("Requested size larger than port MTU (%d)\n", mtu);
							exit(1);
						}
						PRINT_IN_YELLOW("Requested size %lu is compatible with port MTU (%d)\n", msg_size, mtu);
					}
				}
			}
		}
		else // not pingpong server side
		{
			if (qp_type == IBV_QPT_UC && opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
			{
				int wr_index = 0;
				rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);

				memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
				for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
				{

					rr_list[wr_index].next = NULL;
					rr_list[wr_index].wr_id = wr_index + qp_quantity * mr_per_qp;
					rr_list[wr_index].sg_list = NULL;
					rr_list[wr_index].num_sge = 0;
				}
			}
			else if (qp_type == IBV_QPT_UD && opcode == IBV_WR_SEND_WITH_IMM)
			{
				PRINT_IN_PINK("preparing WRS FOR UD SEND WITH IMM\n");
				int wr_index = 0;
				//if (strlen(VersionID) >= strlen(special_prefix) && !memcmp(VersionID, special_prefix, strlen(special_prefix)) && share_mr_between_qp)
				if (share_mr_between_qp && mr_per_qp == 1 && wr_per_qp >= 1)
				{
					rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * wr_per_qp);
					recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge));
					memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * wr_per_qp);
					memset(recv_sge_list, 0, sizeof(struct ibv_sge));
					recv_sge_list[0].addr = (uintptr_t)(res->mr_array[0]->addr);
					recv_sge_list[0].length = mr_size;
					recv_sge_list[0].lkey = res->mr_array[0]->lkey;
					for (wr_index = 0; wr_index < qp_quantity * wr_per_qp; wr_index++)
					{
						if ((wr_index + 1) % wr_per_qp)
						{
							rr_list[wr_index].next = &rr_list[wr_index + 1];
						}
						else
						{
							rr_list[wr_index].next = NULL;
						}
						rr_list[wr_index].wr_id = wr_index + qp_quantity * wr_per_qp;
						rr_list[wr_index].sg_list = &recv_sge_list[0];
						rr_list[wr_index].num_sge = 1;
						if (msg_size == 0)
						{
							rr_list[wr_index].num_sge = 0;
							rr_list[wr_index].sg_list = NULL;
						}
						//PRINT_IN_PINK("lkey: %8x\n", rr_list[wr_index].sg_list->lkey);
					}
				}
				else if (share_mr_between_qp)
				{
					rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
					recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
					memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
					memset(recv_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);

					for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
					{
						if (wr_index < mr_per_qp)
						{
							recv_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
							recv_sge_list[wr_index].length = mr_size;
							recv_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
						}
						if ((wr_index + 1) % mr_per_qp)
						{
							rr_list[wr_index].next = &rr_list[wr_index + 1];
						}
						else
						{
							rr_list[wr_index].next = NULL;
						}
						rr_list[wr_index].wr_id = wr_index + qp_quantity * mr_per_qp;
						rr_list[wr_index].sg_list = &recv_sge_list[wr_index % mr_per_qp];
						rr_list[wr_index].num_sge = 1;
						if (msg_size == 0)
						{
							rr_list[wr_index].num_sge = 0;
							rr_list[wr_index].sg_list = NULL;
						}
						//PRINT_IN_PINK("lkey: %8x\n", rr_list[wr_index].sg_list->lkey);
					}
				}
				else
				{
					rr_list = (struct ibv_recv_wr *)malloc(sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
					recv_sge_list = (struct ibv_sge *)malloc(sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
					memset(rr_list, 0, sizeof(struct ibv_recv_wr) * qp_quantity * mr_per_qp);
					memset(recv_sge_list, 0, sizeof(struct ibv_sge) * qp_quantity * mr_per_qp);
					for (wr_index = 0; wr_index < qp_quantity * mr_per_qp; wr_index++)
					{

						recv_sge_list[wr_index].addr = (uintptr_t)(res->mr_array[wr_index]->addr);
						recv_sge_list[wr_index].length = mr_size;
						recv_sge_list[wr_index].lkey = res->mr_array[wr_index]->lkey;
						if ((wr_index + 1) % mr_per_qp)
						{
							rr_list[wr_index].next = &rr_list[wr_index + 1];
						}
						else
						{
							rr_list[wr_index].next = NULL;
						}
						rr_list[wr_index].wr_id = wr_index + qp_quantity * mr_per_qp;
						rr_list[wr_index].sg_list = &recv_sge_list[wr_index];
						rr_list[wr_index].num_sge = 1;
						if (msg_size == 0)
						{
							rr_list[wr_index].num_sge = 0;
							rr_list[wr_index].sg_list = NULL;
						}
						PRINT_IN_PINK("lkey: %8x\n", rr_list[wr_index].sg_list->lkey);
					}
				}

				{
					struct ibv_port_attr port_info = {};
					int mtu;
					if (ibv_query_port(res->ib_ctx, config.ib_port, &port_info))
					{
						PRINT_IN_RED("Unable to query port info for port %d\n", config.ib_port);
						exit(1);
					}
					mtu = 1 << (port_info.active_mtu + 7);
					if (msg_size > mtu)
					{
						PRINT_IN_RED("Requested size larger than port MTU (%d)\n", mtu);
						exit(1);
					}
					PRINT_IN_YELLOW("Requested size %lu is compatible with port MTU (%d)\n", msg_size, mtu);
				}
			}
		}
	}
	return 0;
}

int post_send(struct resources *res, int i)
{
	int rc = 0;
	struct ibv_send_wr *bad_wr = NULL;
	PRINT_IN_YELLOW("srlist[%d] wrid: %lu \t imm%u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
	if (sr_list[i].imm_data - sr_list[i].wr_id || i - sr_list[i].imm_data)
	{
		//PRINT_IN_RED("sr_list[i].imm_data - sr_list[i].wr_id %lu\ti - sr_list[i].imm_data %u\n",sr_list[i].imm_data - sr_list[i].wr_id,i - sr_list[i].imm_data);
		PRINT_IN_RED("srlist[%d] wrid: %lu \t imm%u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
	}
	PRINT_IN_PINK("qp[%d]:qpn=%u remote qpn = %u remote qpkey = %u\n", i / mr_per_qp, res->qp[i / mr_per_qp]->qp_num, sr_list[i].wr.ud.remote_qpn, sr_list[i].wr.ud.remote_qkey);
	sr_list[i].next = NULL;
	while (rc = ibv_post_send(res->qp[i / mr_per_qp], &sr_list[i], &bad_wr))
	{
		PRINT_IN_RED("srlist[%d] wrid: %lu \t %u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
		PRINT_IN_RED("badwr[%d] wrid: %lu \t %u\n", i, bad_wr->wr_id, bad_wr->imm_data);
		PRINT_IN_RED("rc = %d %s\n", rc, strerror(rc));
		if (rc == 12)
		{
			sr_list[i].send_flags & !IBV_SEND_INLINE;
		}
		PRINT_IN_RED("Error in post send\n");
		check_QP_state(res, "post send");
		error_flag |= 1;
		PRINT_IN_RED("error flag is set to %08x\n", error_flag);
		exit(1);
	}
	return rc;
}
int try_post_send(struct resources *res, int i)
{
	int rc = 0;
	struct ibv_send_wr *bad_wr = NULL;
	PRINT_IN_YELLOW("srlist[%d] wrid: %lu \t imm%u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
	if (sr_list[i].imm_data - sr_list[i].wr_id || i - sr_list[i].imm_data)
	{
		//PRINT_IN_RED("sr_list[i].imm_data - sr_list[i].wr_id %lu\ti - sr_list[i].imm_data %u\n",sr_list[i].imm_data - sr_list[i].wr_id,i - sr_list[i].imm_data);
		PRINT_IN_RED("srlist[%d] wrid: %lu \t imm%u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
	}
	sr_list[i].next = NULL;
	while (rc = ibv_post_send(res->qp[i / mr_per_qp], &sr_list[i], &bad_wr))
	{
		PRINT_IN_RED("srlist[%d] wrid: %lu \t %u\n", i, sr_list[i].wr_id, sr_list[i].imm_data);
		PRINT_IN_RED("badwr[%d] wrid: %lu \t %u\n", i, bad_wr->wr_id, bad_wr->imm_data);
		PRINT_IN_RED("rc = %d %s\n", rc, strerror(rc));
		if (rc == 12)
		{
			sr_list[i].send_flags & !IBV_SEND_INLINE;
		}
		PRINT_IN_RED("Error in post send\n");
		check_QP_state(res, "post send");
		error_flag |= 1;
		PRINT_IN_RED("error flag is set to %08x\n", error_flag);
		return rc;
	}
	return rc;
}

int try_multi_post_send(struct resources *res, int qp_index, struct ibv_send_wr *sl)
{
	int rc = 0;
	struct ibv_send_wr *bad_wr = NULL;
	struct ibv_send_wr *temp = sl;
	int count = 0;
	for (temp = sl; temp; temp = temp->next)
	{
		count++;
		//printf("%lu\t", temp->wr_id);
	}
	//printf("\n");
	PRINT_IN_YELLOW("this time multi post send size is %d\n", count);
	while (rc = ibv_post_send(res->qp[qp_index], sl, &bad_wr))
	{
		PRINT_IN_RED("badwr.wrid: %lu \t %u\n", bad_wr->wr_id, bad_wr->imm_data);
		PRINT_IN_RED("rc = %d %s\n", rc, strerror(rc));
		if (rc == 12)
		{
			bad_wr->send_flags & !IBV_SEND_INLINE;
		}
		PRINT_IN_RED("Error in post send\n");
		check_QP_state(res, "post send");
		error_flag |= 1;
		PRINT_IN_RED("error flag is set to %08x\n", error_flag);
		return rc;
	}
	return rc;
}

int post_recv(struct resources *res, int i)
{
	int rc = 0;
	struct ibv_recv_wr *bad_wr = NULL;
	// PRINT_IN_GREEN("posting wrid %d\n", i);
	PRINT_IN_PINK("qp[%d]:qpn=%u remote qpn = %u\n", i / mr_per_qp, res->qp[i / mr_per_qp]->qp_num, res->remote_props.qp_num[i / mr_per_qp]);

	if (i + qp_quantity * mr_per_qp - rr_list[i].wr_id)
	{
		PRINT_IN_RED("i + qp_quantity * mr_per_qp - rr_list[i].wr_id = %lu\n", i + qp_quantity * mr_per_qp - rr_list[i].wr_id);
		PRINT_IN_RED("rrlist[%d] wrid: %lu\n", i, rr_list[i].wr_id);
	}
	rr_list[i].next = NULL;
	if (rc = ibv_post_recv(res->qp[i / mr_per_qp], &rr_list[i], &bad_wr))
	{
		PRINT_IN_RED("wrid: %lu \t \n", bad_wr->wr_id);
		PRINT_IN_RED("post recv");
		PRINT_IN_RED("Error in post recv\n");
		check_QP_state(res, "post recv");
		exit(1);
	}
	return rc;
}

int multi_post_recv(struct resources *res, int qpindex, struct ibv_recv_wr *wr_l)
{
	int rc = 0;
	struct ibv_recv_wr *bad_wr = NULL;
	// PRINT_IN_GREEN("posting wrid %d\n", i);
	//PRINT_IN_PINK("qp[%d]:qpn=%u remote qpn = %u\n", qpindex, res->qp[qpindex]->qp_num, res->remote_props.qp_num[qpindex]);
	struct ibv_recv_wr *temp = wr_l;
	int count = 0;
	for (temp = wr_l; temp; temp = temp->next)
	{
		count++;
	}
	PRINT_IN_YELLOW("this time multi post recv size is %d\n", count);
	if (rc = ibv_post_recv(res->qp[qpindex], wr_l, &bad_wr))
	{
		PRINT_IN_RED("rc = %d %s\n", rc, strerror(rc));
		PRINT_IN_RED("wrid: %lu \t \n", bad_wr->wr_id);
		PRINT_IN_RED("Error in post recv\n");
		check_QP_state(res, "post recv");
		exit(1);
	}
	return rc;
}

int poll_completion_with_imm(struct resources *res, unsigned long timeout, int *i, int *imm)
{
	struct ibv_wc wc;
	unsigned long start_time_msec;
	unsigned long cur_time_msec;
	struct timeval cur_time;
	int poll_result;
	int rc = 0;

	/* poll the completion for a while before giving up of doing it .. */
	gettimeofday(&cur_time, NULL);
	start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, &wc);

		gettimeofday(&cur_time, NULL);
		cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	} while ((poll_result == 0) &&
			 ((cur_time_msec - start_time_msec) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */
		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
		rc = 1;
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */
		perror("poll");
		PRINT_IN_RED("completion wasn't found in the CQ after timeout\n");
		exit(1);
		rc = 1;
	}
	else
	{
		/* CQE found */
		*i = wc.wr_id;
		*imm = wc.imm_data;
		// PRINT_IN_GREEN("completion was found in CQ with status 0x%x\n", wc.status);
		/* check the completion status (here we don't care about the completion
		 * opcode */
		if (wc.status != IBV_WC_SUCCESS)
		{
			PRINT_IN_RED("got bad completion with status: 0x%x, vendor syndrome: 0x%x\n",
						 wc.status, wc.vendor_err);
			rc = 1;
		}
	}
	return rc;
}

struct ibv_wc *multi_poll_completion_with_imm(struct resources *res, unsigned long timeout, int *i, int *imm, int max_poll_qtt)
{
	struct ibv_wc wc;
	struct ibv_wc *wc_list = (struct ibv_wc *)malloc(sizeof(struct ibv_wc) * max_poll_qtt);
	PRINT_IN_PINK("max poll qtt is %d", max_poll_qtt);
	unsigned long start_time_msec;
	unsigned long cur_time_msec;
	struct timeval cur_time;
	int poll_result;

	/* poll the completion for a while before giving up of doing it .. */
	gettimeofday(&cur_time, NULL);
	start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, wc_list);
		gettimeofday(&cur_time, NULL);
		cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);
	} while ((poll_result == 0) &&
			 ((cur_time_msec - start_time_msec) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */
		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */

		perror("poll");
		PRINT_IN_RED("completion wasn't found in the CQ after timeout\n");
		exit(1);
	}
	else
	{
		return wc_list;
	}
}

int poll_completion(struct resources *res, unsigned long timeout, int *i)
{
	struct ibv_wc wc;
	struct timeval cur_time;
	timeout *= 1000;
	int poll_result;
	int rc = 0;
	size_t ts, te;
	/* poll the completion for a while before giving up of doing it .. */
	ts = get_timestamp();
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, &wc);
		te = get_timestamp();
	} while ((poll_result == 0) &&
			 ((te - ts) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */
		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
		rc = 1;
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */
		PRINT_IN_RED("completion wasn't found in the CQ after timeout\n");
		rc = 1;
		exit(1);
	}
	else
	{

		/* CQE found */
		*i = wc.wr_id;
		if (1)
		{
			char *sc;
			if (*i >= recv_id_offset)
			{
				sc = "recv";
			}
			else
			{
				sc = "send";
			}
			PRINT_IN_PINK("poll %s id %d takes %lu us\n", sc, *i, te - ts);
		}
		if (wc.status != IBV_WC_SUCCESS)
		{
			PRINT_IN_RED("%s\n", ibv_wc_status_str(wc.status));
			exit(1);
			rc = 1;
		}
	}
	return rc;
}

int try_poll_completion(struct resources *res, unsigned long timeout, int *i)
{
	struct ibv_wc wc;
	struct timeval cur_time;
	timeout *= 1000;
	int poll_result;
	int rc = 0;
	size_t ts, te;
	/* poll the completion for a while before giving up of doing it .. */
	ts = get_timestamp();
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, &wc);
		te = get_timestamp();
	} while ((poll_result == 0) &&
			 ((te - ts) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */
		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
		rc = 1;
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */
		PRINT_IN_YELLOW("completion wasn't found in the CQ after timeout\n");
		rc = 1;
	}
	else
	{
		/* CQE found */
		*i = wc.wr_id;
		if (1)
		{
			char *sc;
			if (*i >= recv_id_offset)
			{
				sc = "recv";
			}
			else
			{
				sc = "send";
			}
			PRINT_IN_PINK("poll %s id %d takes %lu us\n", sc, *i, te - ts);
		}
		if (wc.status != IBV_WC_SUCCESS)
		{
			PRINT_IN_RED("%s\n", ibv_wc_status_str(wc.status));
			exit(1);
			rc = 1;
		}
	}
	return rc;
}

int try_poll_completion_with_imm(struct resources *res, unsigned long timeout, int *i, int *imm)
{
	struct ibv_wc wc;
	struct timeval cur_time;
	timeout *= 1000;
	int poll_result;
	int rc = 0;
	size_t ts, te;
	/* poll the completion for a while before giving up of doing it .. */
	ts = get_timestamp();
	do
	{
		poll_result = ibv_poll_cq(res->cq, 1, &wc);
		te = get_timestamp();
	} while ((poll_result == 0) &&
			 ((te - ts) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */
		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
		rc = 1;
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */
		PRINT_IN_YELLOW("completion wasn't found in the CQ after timeout\n");
		rc = 1;
	}
	else
	{
		/* CQE found */
		*i = wc.wr_id;
		*imm = wc.imm_data;
		// PRINT_IN_GREEN("completion was found in CQ with status 0x%x\n", wc.status);
		/* check the completion status (here we don't care about the completion
		 * opcode */
		if (*i >= recv_id_offset)
		{
			PRINT_IN_PINK("poll recv id %d takes %lu us, imm = %d\n", *i, te - ts, *imm);
		}
		if (wc.status != IBV_WC_SUCCESS)
		{
			if (1)
			{
				char *sc;
				if (*i >= recv_id_offset)
				{
					sc = "recv";
					PRINT_IN_RED("poll %s id %d takes %lu us, imm = %d\n", sc, *i, te - ts, *imm);
				}
				else
				{
					sc = "send";
					PRINT_IN_RED("poll %s id %d takes %lu us\n", sc, *i, te - ts);
				}
			}
			PRINT_IN_RED("poll bad status %s\timm: %d \n", ibv_wc_status_str(wc.status), *imm);
			rc = 1;
			check_QP_state(res, "after poll");
			//exit(1);
		}
	}
	return rc;
}

int try_multi_poll_completion(struct resources *res, unsigned long timeout, int max_poll_qtt, struct ibv_wc *wc_list)
{
	struct ibv_wc wc;
	struct timeval cur_time;
	timeout *= 1000;
	int poll_result;
	int rc = 0;
	size_t ts, te;
	if (unsignaled)
	{
		timeout = 0;
	}
	//PRINT_IN_PINK("max poll qtt is %d\n", max_poll_qtt);
	/* poll the completion for a while before giving up of doing it .. */
	ts = get_timestamp();
	do
	{
		poll_result = ibv_poll_cq(res->cq, max_poll_qtt, wc_list);
		te = get_timestamp();
	} while ((poll_result == 0) &&
			 ((te - ts) < timeout));
	if (poll_result < 0)
	{
		/* poll CQ failed */

		PRINT_IN_RED("poll CQ failed\n");
		exit(1);
	}
	else if (poll_result == 0)
	{ /* the CQ is empty */
		if (unsignaled)
		{
			PRINT_IN_PINK("unsignaled no bad cqe is polled\n");
		}
		else
		{
			PRINT_IN_YELLOW("completion wasn't found in the CQ after timeout\n");
		}
	}
	else
	{
		PRINT_IN_PINK("found %d completion\n", poll_result);
	}
	return poll_result;
}

int destroy_sock(struct resources *res)
{
	int rc = 0;
	if (close(res->data_sock))
	{
		PRINT_IN_RED("error in data socket close\n");
		exit(1);
		rc = 1;
	}
	else
	{
		PRINT_IN_YELLOW("successfully close data socket\n");
	}
	if (close(res->ctrl_sock))
	{
		PRINT_IN_RED("error in ctrl socket close\n");
		exit(1);
		rc = 1;
	}
	else
	{
		PRINT_IN_YELLOW("successfully close ctrl socket\n");
	}
	return rc;
}

void print_info()
{
	if (proc_index)
	{
		return;
	}

	char print_string[4096] = "";
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm *lt = localtime(&(tv.tv_sec));
	strftime(time_string, sizeof(time_string), "%m/%d_%H:%M:%S", lt);
	// another version of printfile
	// versionid
	sprintf(print_string, "\n%s\n", VersionID);
	// date time
	sprintf(print_string + strlen(print_string), "%s\n", time_string);
	// thread bound
	sprintf(print_string + strlen(print_string), "thrd_bnd:\t%u\n", thread_bound);
	// qp qtt
	sprintf(print_string + strlen(print_string), "qp_qtt:\t%u\n", qp_quantity);
	// mr per qp
	sprintf(print_string + strlen(print_string), "mr_per_qp\t%u\n", mr_per_qp);
	// mr qtt
	sprintf(print_string + strlen(print_string), "mr_qtt:\t%d\n", mr_quantity);
	// wr per qp
	sprintf(print_string + strlen(print_string), "wr_per_qp:\t%d\n", wr_per_qp);

	//share mr
	if (share_mr_between_qp)
	{
		sprintf(print_string + strlen(print_string), "shared_mr\n");
	}
	else
	{
		sprintf(print_string + strlen(print_string), "dedicated_mr\n");
	}

	// pingpong
	if (pingpong_mode)
	{
		sprintf(print_string + strlen(print_string), "pingpong_on\n");
	}
	else
	{
		sprintf(print_string + strlen(print_string), "pingpong_off\n");
	}

	// mr size
	sprintf(print_string + strlen(print_string), "mr_sz:\t%lu\n", mr_size);
	// msg size
	sprintf(print_string + strlen(print_string), "msg_sz:\t%lu\n", msg_size);
	// thread_quantity
	sprintf(print_string + strlen(print_string), "thrd_qtt:\t%lu\n", thread_quantity);
	// proc_index
	sprintf(print_string + strlen(print_string), "proc_index:\t%d\n", proc_index);

	// proc_quantity
	sprintf(print_string + strlen(print_string), "proc_qtt:\t%d\n", proc_quantity);
	// time_len
	sprintf(print_string + strlen(print_string), "time_len:\t%lu\n", test_time_length);
	// repeattime
	sprintf(print_string + strlen(print_string), "rpt:\t%lu\n", REPEAT);
	// PATHMTU
	sprintf(print_string + strlen(print_string), "pmtu:\t%u\n", path_mtu);
	// inline
	sprintf(print_string + strlen(print_string), "inline:\t%d\n", inline_mode);
	log_verbose_level++;
	PRINT("**************\n%s**************\n", print_string);
	log_verbose_level--;
}

void prt_rst_to_file(size_t tv)
{
	char file_string[4096] = "";

	FILE *outputfp;
	char filename[50];
	sprintf(filename, "%s.log", VersionID);
	outputfp = fopen(filename, "a+");

	// another version of printfile
	// versionid
	sprintf(file_string, "%s\t", VersionID);
	// date time
	sprintf(file_string + strlen(file_string), "%s\t", time_string);
	// subject
	sprintf(file_string + strlen(file_string), "%s\t", test_subject);
	// pingpong mode
	char *pingpong_str;
	if (pingpong_mode)
	{
		pingpong_str = "pingpong_on";
	}
	else
	{
		pingpong_str = "pingpong_off";
	}
	sprintf(file_string + strlen(file_string), "%15s\t", pingpong_str);

	// inline on/off
	char *inline_str;
	if (inline_mode)
	{
		inline_str = "inline_on";
	}
	else
	{
		inline_str = "inline_off";
	}
	sprintf(file_string + strlen(file_string), "%13s\t", inline_str);
	// page_size
	sprintf(file_string + strlen(file_string), "%5s\t", page_size_str);
	// thread bound
	char *thread_bound_str;
	if (thread_bound)
	{
		thread_bound_str = "pin_on";
	}
	else
	{
		thread_bound_str = "pin_off";
	}
	sprintf(file_string + strlen(file_string), "%13s\t", thread_bound_str);
	// qp num
	sprintf(file_string + strlen(file_string), "%6u\t", qp_quantity);
	// mr num
	sprintf(file_string + strlen(file_string), "%6u\t", mr_per_qp);
	// mr size
	sprintf(file_string + strlen(file_string), "%12lu\t", mr_size);
	// msg size
	sprintf(file_string + strlen(file_string), "%12lu\t", msg_size);
	// proc index
	sprintf(file_string + strlen(file_string), "%6d\t", proc_index);
	// proc quantity
	sprintf(file_string + strlen(file_string), "%6d\t", proc_quantity);
	// repeattime
	sprintf(file_string + strlen(file_string), "%8lu\t", REPEAT);
	// test_time_length
	sprintf(file_string + strlen(file_string), "%15lu\t", test_time_length);
	// PATHMTU
	sprintf(file_string + strlen(file_string), "%3u\t", path_mtu);
	// total time
	sprintf(file_string + strlen(file_string), "%20lu\t", tv);

	// for bw
	size_t total_xfer_sz = qp_quantity * mr_per_qp * msg_size * (thread_quantity)*REPEAT;
	double Gb_per_s = total_xfer_sz * (1000000.0 / 1024 / 1024 / 1024) / tv * 8;
	// xfer_size
	sprintf(file_string + strlen(file_string), "%20lu\t", total_xfer_sz);
	// average bw in Gbps
	sprintf(file_string + strlen(file_string), "%3.4lf\t", Gb_per_s);
	// average bw in MBps
	sprintf(file_string + strlen(file_string), "%5.4lf\t", Gb_per_s * 128);

	// for lat
	size_t total_xfer_msg_quantity = qp_quantity * mr_per_qp * thread_quantity * REPEAT;
	if (pingpong_mode)
	{
		total_xfer_msg_quantity *= 2;
	}
	// xfer_msg_quantity
	sprintf(file_string + strlen(file_string), "msg_qtt\t");

	sprintf(file_string + strlen(file_string), "%20lu\t", total_xfer_msg_quantity);
	// average lat in us
	sprintf(file_string + strlen(file_string), "lat\t");

	sprintf(file_string + strlen(file_string), "%3.4lf\t", 1.0 * tv / total_xfer_msg_quantity);

	// for msg_rate
	double msg_rate = 1.0 * total_xfer_msg_quantity / tv;
	sprintf(file_string + strlen(file_string), "msg_rate\t");

	sprintf(file_string + strlen(file_string), "%3.4lf\t", msg_rate);

	// TBC

	// fprints
	fprintf(outputfp, "%s\n", file_string);
	fclose(outputfp);
}

void prt_rst_to_file_fixed_time(size_t total_xfer_quantity, int summary)
{
	char file_string[4096] = "";
	size_t tv = test_time_length;
	FILE *outputfp;
	char filename[50];
	sprintf(filename, "%s.log", VersionID);
	outputfp = fopen(filename, "a+");

	// another version of printfile
	// versionid
	sprintf(file_string, "%s\t", VersionID);
	// date time
	sprintf(file_string + strlen(file_string), "%s\t", time_string);
	// subject
	sprintf(file_string + strlen(file_string), "%s\t", test_subject);
	//transfer type
	char *xfertype;
	if (qp_type == IBV_QPT_RC)
	{
		xfertype = "RC";
	}
	else if (qp_type == IBV_QPT_UC)
	{
		xfertype = "UC";
	}
	else if (qp_type == IBV_QPT_UD)
	{
		xfertype = "UD";
	}
	sprintf(file_string + strlen(file_string), "%4s\t", xfertype);

	//opcode
	char *opcode_str;
	if (opcode == IBV_WR_RDMA_WRITE)
	{
		opcode_str = "WRITE";
	}
	else if (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
	{
		opcode_str = "WRITE_WITH_IMM";
	}
	else if (opcode == IBV_WR_SEND)
	{
		opcode_str = "SEND";
	}
	else if (opcode == IBV_WR_SEND_WITH_IMM)
	{
		opcode_str = "SEND_WITH_IMM";
	}
	sprintf(file_string + strlen(file_string), "%16s\t", opcode_str);
	// pingpong mode
	char *pingpong_str;
	if (pingpong_mode)
	{
		pingpong_str = "pingpong_on";
	}
	else
	{
		pingpong_str = "pingpong_off";
	}
	sprintf(file_string + strlen(file_string), "%15s\t", pingpong_str);

	// inline on/off
	char *inline_str;
	if (inline_mode)
	{
		inline_str = "inline_on";
	}
	else
	{
		inline_str = "inline_off";
	}
	sprintf(file_string + strlen(file_string), "%13s\t", inline_str);

	// page_size
	sprintf(file_string + strlen(file_string), "%s\t", page_size_str);

	// thread bound
	char *thread_bound_str;
	if (thread_bound)
	{
		thread_bound_str = "pin_on";
	}
	else
	{
		thread_bound_str = "pin_off";
	}
	sprintf(file_string + strlen(file_string), "%13s\t", thread_bound_str);
	// qp num
	sprintf(file_string + strlen(file_string), "%6u\t", qp_quantity);
	// mr per qp
	sprintf(file_string + strlen(file_string), "%6u\t", mr_per_qp);
	// wr_quantity
	sprintf(file_string + strlen(file_string), "wr_per_qp\t");
	sprintf(file_string + strlen(file_string), "%6u\t", wr_per_qp);
	// mr qtt
	sprintf(file_string + strlen(file_string), "mr_qtt:\t");
	sprintf(file_string + strlen(file_string), "%6u\t", mr_quantity);

	// share mr
	if (share_mr_between_qp)
	{
		sprintf(file_string + strlen(file_string), "shared_mr\t");
	}
	else
	{
		sprintf(file_string + strlen(file_string), "dedicated_mr\t");
	}

	// mr size
	sprintf(file_string + strlen(file_string), "mr_msg_size\t");
	sprintf(file_string + strlen(file_string), "%12lu\t", mr_size);

	// msg size
	sprintf(file_string + strlen(file_string), "%12lu\t", msg_size);
	// proc index
	if (summary)
	{
		sprintf(file_string + strlen(file_string), "%6d\t", -1);
	}
	else
	{
		sprintf(file_string + strlen(file_string), "%6d\t", proc_index);
	}
	// proc quantity
	sprintf(file_string + strlen(file_string), "%6d\t", proc_quantity);
	// REPEAT
	sprintf(file_string + strlen(file_string), "%8lu\t", REPEAT);
	// test_time_length
	sprintf(file_string + strlen(file_string), "test_time_length:");

	sprintf(file_string + strlen(file_string), "%15lu\t", test_time_length);
	// PATHMTU
	sprintf(file_string + strlen(file_string), "%3u\t", path_mtu);
	// total time
	sprintf(file_string + strlen(file_string), "totaltime:");

	sprintf(file_string + strlen(file_string), "%20lu\t", tv);

	// for bw
	size_t total_xfer_sz;
	total_xfer_sz = total_xfer_quantity * msg_size;
	double Gb_per_s = total_xfer_sz * (1000000.0 / 1024 / 1024 / 1024) / tv * 8;
	// xfer_size
	sprintf(file_string + strlen(file_string), "xfer_size\t");

	sprintf(file_string + strlen(file_string), "%20lu\t", total_xfer_sz);
	sprintf(file_string + strlen(file_string), "bw\t");

	// average bw in Gbps
	sprintf(file_string + strlen(file_string), "%3.4lf\t", Gb_per_s);
	// average bw in MBps
	sprintf(file_string + strlen(file_string), "%5.4lf\t", Gb_per_s * 128);

	// for lat
	size_t total_xfer_msg_quantity = total_xfer_quantity;
	if (pingpong_mode)
	{
		total_xfer_msg_quantity *= 2;
	}
	// xfer_msg_quantity
	sprintf(file_string + strlen(file_string), "msg_qtt\t");
	sprintf(file_string + strlen(file_string), "%20lu\t", total_xfer_msg_quantity);
	// average lat in us
	sprintf(file_string + strlen(file_string), "lat\t");
	if (summary)
	{
		sprintf(file_string + strlen(file_string), "%3.4lf\t", 1.0 * tv * proc_quantity * qp_quantity * mr_per_qp / total_xfer_msg_quantity);
	}
	else
	{
		sprintf(file_string + strlen(file_string), "%3.4lf\t", 1.0 * tv * qp_quantity * mr_per_qp / total_xfer_msg_quantity);
	}
	// for msg_rate
	double msg_rate = 1.0 * total_xfer_quantity / tv;
	sprintf(file_string + strlen(file_string), "msg_rate\t");
	sprintf(file_string + strlen(file_string), "%3.4lf\t", msg_rate);
	// received ratio
	double ratio = 0;
	if (summary)
	{
		{
			size_t total_recv_quantitiy = 0;
			for (int i = 0; i < proc_quantity; i++)
			{
				total_recv_quantitiy += pshemem_struct->recved_quantity[i];
			}
			ratio = 1.0 * total_recv_quantitiy / total_xfer_quantity;
		}
	}
	else
	{
		ratio = recv_qtt_tmp / total_xfer_quantity;
	}
	sprintf(file_string + strlen(file_string), "ratio\t");

	sprintf(file_string + strlen(file_string), "%3.4lf\t", ratio);

	//error flag
	sprintf(file_string + strlen(file_string), "err_flag\t");
	sprintf(file_string + strlen(file_string), "%08x\t", error_flag);

	// TBC

	// fprints
	fprintf(outputfp, "%s\n", file_string);
	fclose(outputfp);
}

void print_final_result(size_t tv)
{
	if (config.server_name)
	{
		if (!strcmp(test_subject, "bw"))
		{
			size_t total_xfer_sz = qp_quantity * mr_per_qp * msg_size * (thread_quantity)*REPEAT;
			double Gb_per_s = total_xfer_sz * (1000000.0 / 1024 / 1024 / 1024) / tv * 8;
			printf("Final RESULT:\n");
			printf("time stamp: %s", time_string);
			printf("qp_quantity = %d\tmr_per_qp = %d\tMR_SIZE = %lu\tthread_quantity = %lu\n", qp_quantity, mr_per_qp, mr_size, thread_quantity);
			printf("xfer_size:%lfMB in %lf ms\n", total_xfer_sz / 1024.0 / 1024, tv / 1000.0);
			printf("%lfGb/s\n", Gb_per_s);
			printf("%lfMB/s\n", Gb_per_s * 128);
			printf("RESULT ends\n");
			prt_rst_to_file(tv);
		}
		else if (!strcmp(test_subject, "lat"))
		{
			size_t total_xfer_msg_quantity = qp_quantity * mr_per_qp * thread_quantity * REPEAT;
			if (pingpong_mode)
			{
				total_xfer_msg_quantity *= 2;
			}
			printf("time stamp: %s", time_string);
			printf("qp_quantity = %d\tmr_per_qp = %d\tMR_SIZE = %lu\tthread_quantity = %lu\n", qp_quantity, mr_per_qp, mr_size, thread_quantity);
			printf("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
			printf("average lat:\t%lfus\n", 1.0 * tv / total_xfer_msg_quantity);
			printf("RESULT ends\n");
			prt_rst_to_file(tv);
		}
		else if (!strcmp(test_subject, "msg_rate"))
		{
			size_t total_xfer_msg_quantity = qp_quantity * mr_per_qp * thread_quantity * REPEAT;
			double msg_rate = 1.0 * total_xfer_msg_quantity / tv;
			printf("time stamp: %s", time_string);
			printf("qp_quantity = %d\tmr_per_qp = %d\tMR_SIZE = %lu\tthread_quantity = %lu\n", qp_quantity, mr_per_qp, mr_size, thread_quantity);
			printf("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
			printf("average msg_rate:\t%lf messages per us (million messages/s)\n", msg_rate);
			printf("RESULT ends\n");
			prt_rst_to_file(tv);
		}
		else
		{
			PRINT_IN_RED("subject ERROR\n");
			exit(1);
		}
	}
	else
	{
		// no nothing
	}
}

void print_final_result_fixed_time_length()
{
	if (proc_index)
	{
		return;
	}
	else if (!config.server_name)
	{
		uint32_t errflag_tosent = htonl(pshemem_struct->error_flag);
		uint32_t dumpint = 0;
		if (pshemem_struct->error_flag)
		{
			PRINT_IN_RED("final error flag is %08x\n", pshemem_struct->error_flag);
		}
		if (sock_sync(res.data_sock, sizeof(uint32_t), (void *)&errflag_tosent, (void *)&dumpint))
		{
			PRINT_IN_RED("error in sending errflag\n");
		}
		return;
	}
	uint32_t errflag_tmp, rem_errflag;
	uint32_t dumpint = 0;
	if (pshemem_struct->error_flag)
	{
		PRINT_IN_RED("final error flag is %08x\n", pshemem_struct->error_flag);
	}

	if (sock_sync(res.data_sock, sizeof(uint32_t), (void *)&dumpint, (void *)&errflag_tmp))
	{
		PRINT_IN_RED("error in recving errflag\n");
	}
	rem_errflag = ntohl(errflag_tmp);
	error_flag = pshemem_struct->error_flag;
	error_flag |= rem_errflag;

	size_t pp_count[100];
	size_t total_pp_count = 0;
	size_t post_quantity[100];
	size_t total_post_qtt = 0;
	size_t recv_quantitiy[100];
	size_t total_receiv_qtt = 0;
	for (int i = 0; i < proc_quantity; i++)
	{
		post_quantity[i] = pshemem_struct->msg_post_quantity[i];
		PRINT_IN_PINK("post_quantity[%d]: %lu\n", i, post_quantity[i]);
		total_post_qtt += post_quantity[i];
	}
	PRINT_IN_PINK("total post qtt %lu\n", total_post_qtt);
	for (int i = 0; i < proc_quantity; i++)
	{
		recv_quantitiy[i] = pshemem_struct->recved_quantity[i];
		PRINT_IN_PINK("recv_quantity[%d]: %lu\n", i, recv_quantitiy[i]);
		total_receiv_qtt += recv_quantitiy[i];
	}
	PRINT_IN_PINK("total recv qtt %lu\n", total_receiv_qtt);
	for (int i = 0; i < proc_quantity; i++)
	{
		pp_count[i] = pshemem_struct->pingpong_count[i];
		PRINT_IN_PINK("ppcount[%d]: %lu\n", i, pp_count[i]);
		total_pp_count += pp_count[i];
	}
	size_t tv = test_time_length;
	if (config.server_name)
	{
		for (int i = 0; i < proc_quantity; i++)
		{
			if (!strcmp(test_subject, "bw"))
			{
				size_t total_xfer_sz = pp_count[i] * msg_size;
				double Gb_per_s = total_xfer_sz * (1000000.0 / 1024 / 1024 / 1024) / tv * 8;
				PRINT("Proc RESULT:\n");
				PRINT("time stamp: %s", time_string);
				PRINT("qp_quantity = %d\tmr_per_qp = %d\t MR_SIZE = %lu\tMSG_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, msg_size, proc_quantity);
				PRINT("xfer_size:%lfMB in %lf ms\n", total_xfer_sz / 1024.0 / 1024, tv / 1000.0);
				PRINT("%lfGb/s\n", Gb_per_s);
				PRINT("%lfMB/s\n", Gb_per_s * 128);
				PRINT("RESULT ends\n");
				//prt_rst_to_file_fixed_time(pp_count[i], 0);
			}
			else if (!strcmp(test_subject, "lat"))
			{
				size_t total_xfer_msg_quantity = pp_count[i];
				if (pingpong_mode)
				{
					total_xfer_msg_quantity *= 2;
				}
				PRINT("Proc RESULT:\ntime stamp: %s", time_string);
				PRINT("qp_quantity = %d\tmr_per_qp = %d\t MR_SIZE = %lu\tMSG_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, msg_size, proc_quantity);
				PRINT("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
				PRINT("average lat:\t%lfus\n", 1.0 * tv / total_xfer_msg_quantity);
				PRINT("RESULT ends\n");
				//prt_rst_to_file_fixed_time(pp_count[i], 0);
			}
			else if (!strcmp(test_subject, "msg_rate"))
			{
				size_t total_xfer_msg_quantity = post_quantity[i];
				double msg_rate = 1.0 * total_xfer_msg_quantity / tv;
				PRINT("Proc RESULT:\ntime stamp: %s", time_string);
				PRINT("qp_quantity = %d\tmr_per_qp = %d\t MR_SIZE = %lu\tMSG_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, msg_size, proc_quantity);
				PRINT("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
				PRINT("average msg_rate:\t%lf messages per us (million messages/s)\n", msg_rate);
				PRINT("RESULT ends\n");
				recv_qtt_tmp = recv_quantitiy[i];
				//prt_rst_to_file_fixed_time(post_quantity[i], 0);
			}
			else
			{
				PRINT_IN_RED("subject ERROR\n");
			}
		}
		// summary of all procs
		if (!strcmp(test_subject, "bw"))
		{
			size_t total_xfer_sz = total_pp_count * msg_size;
			double Gb_per_s = total_xfer_sz * (1000000.0 / 1024 / 1024 / 1024) / tv * 8;
			PRINT("Final RESULT:\n");
			PRINT("time stamp: %s", time_string);
			PRINT("qp_quantity = %d\tmr_per_qp = %d\tMR_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, proc_quantity);
			PRINT("xfer_size:%lfMB in %lf ms\n", total_xfer_sz / 1024.0 / 1024, tv / 1000.0);
			PRINT("%lfGb/s\n", Gb_per_s);
			PRINT("%lfMB/s\n", Gb_per_s * 128);
			PRINT("RESULT ends\n");
			prt_rst_to_file_fixed_time(total_pp_count, 1);
		}
		else if (!strcmp(test_subject, "lat"))
		{
			size_t total_xfer_msg_quantity = total_pp_count;
			if (pingpong_mode)
			{
				total_xfer_msg_quantity *= 2;
			}
			PRINT("Final RESULT:\n");
			PRINT("time stamp: %s", time_string);
			PRINT("qp_quantity = %d\tmr_per_qp = %d\tMR_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, proc_quantity);
			PRINT("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
			PRINT("average lat:\t%lfus\n", 1.0 * tv * proc_quantity / total_xfer_msg_quantity);
			PRINT("RESULT ends\n");
			prt_rst_to_file_fixed_time(total_pp_count, 1);
		}
		else if (!strcmp(test_subject, "msg_rate"))
		{
			size_t total_xfer_msg_quantity = total_post_qtt;
			double msg_rate = 1.0 * total_xfer_msg_quantity / tv;
			PRINT("Final RESULT:\n");
			PRINT("time stamp: %s", time_string);
			PRINT("qp_quantity = %d\tmr_per_qp = %d\t MR_SIZE = %lu\tMSG_SIZE = %lu\tproc_quantity = %d\n", qp_quantity, mr_per_qp, mr_size, msg_size, proc_quantity);
			PRINT("total_xfer_msg_quantity:%lu\n", total_xfer_msg_quantity);
			PRINT("average msg_rate:\t%lf messages per us (million messages/s)\n", msg_rate);
			PRINT("RESULT ends\n");
			prt_rst_to_file_fixed_time(total_post_qtt, 1);
		}
	}
	else
	{
		// no nothing
	}
}

void alloc_buffer_huge_page(struct resources *res)
{
	void *addr, *addr2 = NULL;
	total_mmap_size = msg_size * qp_quantity * mr_per_qp * thread_quantity;
	if (pingpong_mode)
	{
		total_mmap_size *= 2;
	}
	PRINT_IN_YELLOW("before temp calculation\n");

	size_t temp = total_mmap_size / page_size * page_size;
	if (total_mmap_size % page_size)
	{
		total_mmap_size = temp + page_size;
	}
	else
	{
		total_mmap_size = temp;
	}
	PRINT_IN_YELLOW("after temp calculation\n");

	res->buf = mmap(NULL, total_mmap_size, (PROT_READ | PROT_WRITE), (MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB), -1, 0);
	if (res->buf == MAP_FAILED)
	{
		PRINT_IN_YELLOW("ERROR of MMAP\n");
		perror("mmap");
		exit(1);
	}

	// printf("%s", str);
}
size_t test_core(struct resources *res)
{
	if (!strcmp(test_subject, "bw"))
	{
		return test_bw(res);
	}
	else if (!strcmp(test_subject, "lat"))
	{

		return test_lat(res);
		// return 10;
	}
	else if (!strcmp(test_subject, "msg_rate"))
	{
		return test_msg_rate(res);
	}
}

size_t test_core_pingpong(struct resources *res)
{
	if (!strcmp(test_subject, "bw"))
	{
		if (opcode == IBV_WR_SEND_WITH_IMM)
		{
			return test_bw_pingpong_timelength(res);
		}
		else if (opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
		{
			return test_bw_pingpong_timelength(res);
		}
		else
		{
			PRINT_IN_RED("wrong opcode in test core pingpong\n");
			exit(1);
		}
	}
	else if (!strcmp(test_subject, "lat"))
	{

		return test_bw_pingpong_timelength(res);
		// return 10;
	}
	else
	{
		PRINT_IN_RED("subject error\n");
		exit(1);
	}
}

//multi poll version
size_t test_bw_pingpong_timelength(struct resources *res)
{
	if (!mr_pingpong_count)
	{
		mr_pingpong_count = malloc(sizeof(size_t) * qp_quantity * mr_per_qp);
	}
	memset(mr_pingpong_count, 0, sizeof(size_t) * qp_quantity * mr_per_qp);

	/* 	int poll_counter[qp_quantity * mr_per_qp];
	memset(poll_counter, 0, sizeof(int) * qp_quantity * mr_per_qp);
 */
	int rest_quantitity_to_poll_send;
	int rest_quantitity_to_poll_recv;
	int send_count = 0;
	for (int i = 0; i < qp_quantity; i++)
	{
		//post_recv(res, i);
		multi_post_recv(res, i, &rr_list[i * mr_per_qp]);
	}
	barrier_sem_inter_intra_nodes();

	size_t ts, te;
	memset(mr_pingpong_count, 0, sizeof(size_t) * qp_quantity * mr_per_qp);

	PRINT_IN_PINK("warmup ends\n");

	barrier_sem_inter_intra_nodes();
	//memset(poll_counter, 0, sizeof(int) * qp_quantity * mr_per_qp);
	struct ibv_recv_wr **recv_wr_ptr_list = (struct ibv_recv_wr **)malloc(sizeof(struct ibv_recv_wr *) * qp_quantity);
	struct ibv_send_wr **send_wr_ptr_list = (struct ibv_send_wr **)malloc(sizeof(struct ibv_send_wr *) * qp_quantity);
	struct ibv_wc *wc_list = NULL;
	wc_list = (struct ibv_wc *)malloc(sizeof(struct ibv_wc) * global_max_poll_quantity);
	memset(wc_list, 0, sizeof(struct ibv_wc) * global_max_poll_quantity);
	if (config.server_name)
	{
		ts = get_timestamp();
		for (int i = 0; i < qp_quantity; i++)
		{
			//post_send(res, i);
			//PRINT_IN_PINK("before send qp[%d]\n", i);
			try_multi_post_send(res, i, &sr_list[i * mr_per_qp]);
		}
		PRINT_IN_PINK("client first posting send over \n");
	}
	else
	{
		ts = get_timestamp();
	}
	int counter = 0;

	for (te = get_timestamp(); !config.server_name && te - ts < (size_t)(test_time_length * 1.05) || te - ts < test_time_length; te = get_timestamp())
	{
		//PRINT_IN_PINK("counter %d\n",counter++);
		int temp, imm;
		int polled_qtt;
		// check_QP_state(res, "before poll");
		if (!(polled_qtt = try_multi_poll_completion(res, TRY_TIMEOUT, global_max_poll_quantity, wc_list)))
		{
			//PRINT_IN_PINK("te - ts: %lu\t set_length: %lu \t diff_ratio: %.6lf\n", te - ts, test_time_length, 1.0 * (te - ts) / test_time_length - 1);
			if (!config.server_name && te - ts > (size_t)(test_time_length * 1.1))
			{
				break;
			}
			PRINT_IN_PINK("try poll failed\n");
			check_QP_state(res, "after poll");
			continue;
		}
		//head node list

		memset(send_wr_ptr_list, 0, sizeof(struct ibv_send_wr *) * qp_quantity);
		memset(recv_wr_ptr_list, 0, sizeof(struct ibv_recv_wr *) * qp_quantity);

		//one head node for a queue

		for (int wc_index = 0; wc_index < polled_qtt; wc_index++)
		{
			struct ibv_wc cur_wqe = wc_list[wc_index];

			//PRINT_IN_PINK("in the loop\n");
			if (wc_list[wc_index].wr_id >= recv_id_offset)
			{

				int mr_id = wc_list[wc_index].imm_data;
				int qp_id = mr_id / mr_per_qp;
				if (mr_id >= qp_quantity * mr_per_qp)
				{

					PRINT_IN_RED("mr index out of bound %d, wcwr id: %lu\n", mr_id, wc_list[wc_index].wr_id);
					exit(1);
				}
				if (qp_id >= qp_quantity)
				{
					PRINT_IN_RED("qp index out of bound %d\n", qp_id);
					exit(1);
				}
				mr_pingpong_count[wc_list[wc_index].imm_data]++;
				PRINT_IN_YELLOW("polled cqe of recv wrid %lu, imm data %u\n", wc_list[wc_index].wr_id, wc_list[wc_index].imm_data);
				PRINT_IN_YELLOW("polled cqe of recv %d, qp id %d\n", mr_id, qp_id);
				if (cur_wqe.status != IBV_WC_SUCCESS)
				{
					PRINT_IN_RED("%s\n", ibv_wc_status_str(cur_wqe.status));
					exit(1);
				}
				if (recv_wr_ptr_list[qp_id] == NULL)
				{
					recv_wr_ptr_list[qp_id] = &rr_list[mr_id];
					recv_wr_ptr_list[qp_id]->next = recv_wr_ptr_list[qp_id];
					send_wr_ptr_list[qp_id] = &sr_list[mr_id];
					send_wr_ptr_list[qp_id]->next = send_wr_ptr_list[qp_id];
				}
				else
				{
					rr_list[mr_id].next = recv_wr_ptr_list[qp_id]->next;
					recv_wr_ptr_list[qp_id] = recv_wr_ptr_list[qp_id]->next = &rr_list[mr_id];
					sr_list[mr_id].next = send_wr_ptr_list[qp_id]->next;
					send_wr_ptr_list[qp_id] = send_wr_ptr_list[qp_id]->next = &sr_list[mr_id];
				}
			}
			else
			{
				PRINT_IN_YELLOW("polled send wr id %lu\n", wc_list[wc_index].wr_id);
				if (cur_wqe.status != IBV_WC_SUCCESS)
				{
					PRINT_IN_RED("%s\n", ibv_wc_status_str(cur_wqe.status));
					exit(1);
				}
				// poll_counter[temp]++;
			}
		}

		struct ibv_send_wr *temp_send;
		struct ibv_recv_wr *temp_recv;
		for (int i = 0; i < qp_quantity; i++)
		{
			if (recv_wr_ptr_list[i])
			{
				//adjust the head node
				temp_recv = recv_wr_ptr_list[i];
				recv_wr_ptr_list[i] = recv_wr_ptr_list[i]->next;
				temp_recv->next = NULL;

				temp_send = send_wr_ptr_list[i];
				send_wr_ptr_list[i] = send_wr_ptr_list[i]->next;
				temp_send->next = NULL;

				//multipost
				//PRINT_IN_YELLOW("before multipost recv qp[%d]\n", i);
				multi_post_recv(res, i, recv_wr_ptr_list[i]);
				//PRINT_IN_YELLOW("before multipost send qp[%d]\n", i);
				try_multi_post_send(res, i, send_wr_ptr_list[i]);
			}
		}
	}

	if (recv_wr_ptr_list)
	{
		free(recv_wr_ptr_list);
		recv_wr_ptr_list = NULL;
	}
	if (send_wr_ptr_list)
	{
		free(send_wr_ptr_list);
		send_wr_ptr_list = NULL;
	}
	if (wc_list)
	{
		free(wc_list);
		wc_list = NULL;
	}
	size_t total_pingpong_count = 0;
	sem_t *sem_shmem = sem_open_existing(sem_name_shmem_access);
	PRINT_IN_PINK("te - ts: %lu\t set_length: %lu \t diff_ratio: %.6lf\n", te - ts, test_time_length, 1.0 * (te - ts) / test_time_length - 1);
	for (int i = 0; i < qp_quantity * mr_per_qp; i++)
	{
		PRINT_IN_PINK("[%d]:%lu\n", i, mr_pingpong_count[i]);
		total_pingpong_count += mr_pingpong_count[i];
	}
	PRINT_IN_PINK("total pingpongcount:%lu\n", total_pingpong_count);
	size_t recved_total_pingpong_count = 0;
	if (sock_sync(res->data_sock, sizeof(total_pingpong_count), (void *)&total_pingpong_count, (void *)&recved_total_pingpong_count))
	{
		PRINT_IN_RED("error in receiving total pingpong count from opp side\n");
	}
	else
	{
		PRINT_IN_YELLOW("recved pingpong count from opp side : %lu\n", recved_total_pingpong_count);
	}

	if (config.server_name)
	{
		sem_wait(sem_shmem);
		pshemem_struct->pingpong_count[proc_index] = total_pingpong_count;
		pshemem_struct->msg_post_quantity[proc_index] = total_pingpong_count;
		pshemem_struct->recved_quantity[proc_index] = recved_total_pingpong_count;
		sem_post(sem_shmem);
	}
	else
	{
	}
	sem_wait(sem_shmem);
	pshemem_struct->error_flag |= error_flag;
	sem_post(sem_shmem);
	barrier_sem_inter_intra_nodes();
	return 0;
}

size_t test_msg_rate(struct resources *res)
{
	if (qp_type == IBV_QPT_UD)
	{
		PRINT_IN_PINK("SWITCHING TO test msg rate UD\n");
		return test_msg_rate_ud(res);
	}
	if (config.server_name)
	{
		size_t total_time = 0;
		size_t post_seq = 0;
		size_t poll_seq = 0;
		char temp_char;
		barrier_sem_inter_intra_nodes();
		size_t ts, te;
		ts = get_timestamp();
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			post_send(res, i);
			PRINT_IN_GREEN("NUMBER %d is posted wr_id %d\n", i, i);
			post_seq++;
		}
		for (te = get_timestamp(); te - ts < test_time_length; te = get_timestamp())
		{
			int temp;
			try_poll_completion(res, TIMEOUT, &temp);
			// PRINT_IN_BLUE("NUMBER %lu is polled\n",poll_seq++);
			post_send(res, temp);
			//poll_seq++;
			post_seq++;
			PRINT_IN_GREEN("NUMBER %lu is posted wr_id %d\n", post_seq, temp);
		}
		PRINT_IN_YELLOW("post_seq:%lu\n", post_seq);
		//PRINT_IN_YELLOW("poll_seq:%lu\n", poll_seq);
		size_t sync_temp;

		if (sock_sync(res->ctrl_sock, sizeof(size_t), (void *)&sync_temp, (void *)&received_quantity))
		{
			PRINT_IN_RED("failed to receive the recv_quantity from opposite side\n");
			exit(1);
		}
		else
		{
			PRINT_IN_PINK("received quantity[%d]: %lu\n", proc_index, received_quantity);
		}
		sem_t *sem_shmem = sem_open_existing(sem_name_shmem_access);
		sem_wait(sem_shmem);
		pshemem_struct->msg_post_quantity[proc_index] = post_seq;
		pshemem_struct->recved_quantity[proc_index] = received_quantity;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		barrier_sem_inter_intra_nodes();
		return post_seq;
	}
	else // server side
	{
		size_t total_time = 0;
		size_t post_seq = 0;
		size_t poll_seq = 0;
		size_t warm_up_loop_time = REPEAT / 4;
		char temp_char;
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			post_recv(res, i);
			// post_seq++;
			// PRINT_IN_BLUE("NUMBER %d is posted\n",i);
		}
		barrier_sem_inter_intra_nodes(sem_name_test_ready, sem_name_test_start);
		size_t ts, te;
		ts = get_timestamp();
		for (te = get_timestamp(); te - ts < (size_t)(test_time_length * 1.05); te = get_timestamp())
		{
			int temp;
			if (try_poll_completion(res, TRY_TIMEOUT, &temp))
			{
				PRINT_IN_PINK("try poll failed\n");
				continue;
			}
			else
			{
				PRINT_IN_BLUE("NUMBER %lu is polled wr_id %d\n", poll_seq, temp - recv_id_offset);
			}
			poll_seq++;

			post_recv(res, temp - recv_id_offset);
		}
		PRINT_IN_PINK("poll_seq[%d]:%lu\n", proc_index, poll_seq);
		size_t sync_temp;
		if (sock_sync(res->ctrl_sock, sizeof(size_t), (void *)&poll_seq, (void *)&sync_temp))
		{
			PRINT_IN_RED("failed to send the recv_quantity from opposite side\n");
			exit(1);
		}
		else
		{
			PRINT_IN_PINK("Successfully send received quantity: %lu\n", poll_seq);
		}

		barrier_sem_inter_intra_nodes();
		return 0;
	}
}

size_t test_msg_rate_ud(struct resources *res)
{
	PRINT_IN_PINK("this is the start of test_msg_rate_ud\n");
	if (!mr_pingpong_count)
	{
		mr_pingpong_count = malloc(sizeof(size_t) * qp_quantity * (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp));
	}
	memset(mr_pingpong_count, 0, sizeof(size_t) * qp_quantity * (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp));
	int unit = (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp);

	if (config.server_name)
	{
		size_t total_time = 0;
		size_t total_polled_qtt = 0;
		char temp_char;
		size_t warm_up_loop_time = REPEAT / 4;
		barrier_sem_inter_intra_nodes();
		size_t ts, te;
		ts = get_timestamp();
		for (int i = 0; i < qp_quantity; i++)
		{
			//post_send(res, i);
			PRINT_IN_PINK("before send qp[%d]\n", i);
			try_multi_post_send(res, i, &sr_list[i * unit]);
		}

		for (te = get_timestamp(); !config.server_name && te - ts < (size_t)(test_time_length * 1.05) || te - ts < test_time_length; te = get_timestamp())
		{
			//PRINT_IN_PINK("counter %d\n",counter++);
			int temp, imm;
			int polled_qtt;
			struct ibv_wc *wc_list = NULL;
			wc_list = (struct ibv_wc *)malloc(sizeof(struct ibv_wc) * global_max_poll_quantity);
			// check_QP_state(res, "before poll");
			if (!(polled_qtt = try_multi_poll_completion(res, TRY_TIMEOUT, global_max_poll_quantity, wc_list)))
			{
				PRINT_IN_PINK("te - ts: %lu\t set_length: %lu \t diff_ratio: %.6lf\n", te - ts, test_time_length, 1.0 * (te - ts) / test_time_length - 1);
				if (!config.server_name && get_timestamp() - ts > (size_t)(test_time_length * 1.1))
				{
					break;
				}
				PRINT_IN_PINK("try poll failed\n");
				check_QP_state(res, "after poll");
				if (wc_list)
				{
					free(wc_list);
					wc_list = NULL;
				}
				continue;
			}
			//head node list
			struct ibv_send_wr **send_wr_ptr_list = (struct ibv_send_wr **)malloc(sizeof(struct ibv_send_wr *) * qp_quantity);
			memset(send_wr_ptr_list, 0, sizeof(struct ibv_send_wr *) * qp_quantity);

			//one head node for a queue
			total_polled_qtt += polled_qtt;
			for (int wc_index = 0; wc_index < polled_qtt; wc_index++)
			{
				int mr_id = wc_list[wc_index].wr_id % (qp_quantity * unit);
				int qp_id = mr_id / unit;

				//PRINT_IN_YELLOW("polled cqe of send %d, qp id %d\n", mr_id, qp_id);
				if (wc_list[wc_index].status != IBV_WC_SUCCESS)
				{
					PRINT_IN_RED("poll bad status %s\t\n", ibv_wc_status_str(wc_list[wc_index].status));
					exit(1);
				}
				mr_pingpong_count[mr_id]++;
				if (send_wr_ptr_list[qp_id] == NULL)
				{
					send_wr_ptr_list[qp_id] = &sr_list[mr_id];
					send_wr_ptr_list[qp_id]->next = send_wr_ptr_list[qp_id];
				}
				else
				{
					sr_list[mr_id].next = send_wr_ptr_list[qp_id]->next;
					send_wr_ptr_list[qp_id] = send_wr_ptr_list[qp_id]->next = &sr_list[mr_id];
				}
			}
			if (wc_list)
			{
				free(wc_list);
				wc_list = NULL;
			}
			struct ibv_send_wr *temp_send;
			struct ibv_recv_wr *temp_recv;
			for (int i = 0; i < qp_quantity; i++)
			{
				if (send_wr_ptr_list[i])
				{
					//adjust the head node
					temp_send = send_wr_ptr_list[i];
					send_wr_ptr_list[i] = send_wr_ptr_list[i]->next;
					temp_send->next = NULL;

					//multipost
					PRINT_IN_PINK("before multipost send qp[%d]\n", i);
					PRINT_IN_PINK("QPN[%d] is %u\n", i, res->qp[i]->qp_num);
					try_multi_post_send(res, i, send_wr_ptr_list[i]);
					PRINT_IN_PINK("after multipost send qp[%d]\n", i);
				}
			}
			if (send_wr_ptr_list)
			{
				free(send_wr_ptr_list);
				send_wr_ptr_list = NULL;
			}
		}

		//PRINT_IN_YELLOW("poll_seq:%lu\n", poll_seq);
		size_t sync_temp;

		if (sock_sync(res->ctrl_sock, sizeof(size_t), (void *)&sync_temp, (void *)&received_quantity))
		{
			PRINT_IN_RED("failed to receive the recv_quantity from opposite side\n");
			exit(1);
		}
		else
		{
			PRINT_IN_PINK("received quantity[%d]: %lu\n", proc_index, received_quantity);
		}
		/* 		if(received_quantity>=total_polled_qtt){
			PRINT_IN_RED("received: %lu, sended: %lu, diff: %lu \n",received_quantity,total_polled_qtt,received_quantity - total_polled_qtt);
		} */
		sem_t *sem_shmem = sem_open_existing(sem_name_shmem_access);
		sem_wait(sem_shmem);
		pshemem_struct->msg_post_quantity[proc_index] = total_polled_qtt;
		pshemem_struct->recved_quantity[proc_index] = received_quantity;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		barrier_sem_inter_intra_nodes();
		return total_polled_qtt;
	}
	else // server side
	{
		size_t total_time = 0;

		size_t warm_up_loop_time = REPEAT / 4;
		char temp_char;
		for (int i = 0; i < qp_quantity; i++)
		{
			//post_recv(res, i);
			multi_post_recv(res, i, &rr_list[i * unit]);
		}
		barrier_sem_inter_intra_nodes();
		size_t ts, te;
		ts = get_timestamp();
		size_t total_polled_qtt = 0;
		for (te = get_timestamp(); !config.server_name && te - ts < (size_t)(test_time_length * 1.05) || te - ts < test_time_length; te = get_timestamp())
		{
			//PRINT_IN_PINK("counter %d\n",counter++);
			int temp, imm;
			int polled_qtt;
			struct ibv_wc *wc_list = NULL;
			wc_list = (struct ibv_wc *)malloc(sizeof(struct ibv_wc) * global_max_poll_quantity);
			// check_QP_state(res, "before poll");
			if (!(polled_qtt = try_multi_poll_completion(res, TRY_TIMEOUT, global_max_poll_quantity, wc_list)))
			{
				PRINT_IN_PINK("te - ts: %lu\t set_length: %lu \t diff_ratio: %.6lf\n", te - ts, test_time_length, 1.0 * (te - ts) / test_time_length - 1);
				if (!config.server_name && te - ts > (size_t)(test_time_length * 1.1))
				{
					break;
				}
				PRINT_IN_PINK("try poll failed\n");
				check_QP_state(res, "after poll");
				if (wc_list)
				{
					free(wc_list);
					wc_list = NULL;
				}
				continue;
			}
			total_polled_qtt += polled_qtt;
			//head node list
			struct ibv_recv_wr **recv_wr_ptr_list = (struct ibv_recv_wr **)malloc(sizeof(struct ibv_recv_wr *) * qp_quantity);
			memset(recv_wr_ptr_list, 0, sizeof(struct ibv_recv_wr *) * qp_quantity);

			//one head node for a queue

			for (int wc_index = 0; wc_index < polled_qtt; wc_index++)
			{
				int mr_id = wc_list[wc_index].wr_id % (qp_quantity * unit);
				int qp_id = mr_id / unit;
				//PRINT_IN_YELLOW("polled cqe of recv %d, qp id %d, wrid %lu\n", mr_id, qp_id, wc_list[wc_index].wr_id);
				if (wc_list[wc_index].status != IBV_WC_SUCCESS)
				{
					PRINT_IN_RED("poll bad status %s\t\n", ibv_wc_status_str(wc_list[wc_index].status));
					exit(1);
				}
				mr_pingpong_count[mr_id]++;
				if (recv_wr_ptr_list[qp_id] == NULL)
				{
					recv_wr_ptr_list[qp_id] = &rr_list[mr_id];
					recv_wr_ptr_list[qp_id]->next = recv_wr_ptr_list[qp_id];
				}
				else
				{
					rr_list[mr_id].next = recv_wr_ptr_list[qp_id]->next;
					recv_wr_ptr_list[qp_id] = recv_wr_ptr_list[qp_id]->next = &rr_list[mr_id];
				}
			}
			if (wc_list)
			{
				free(wc_list);
				wc_list = NULL;
			}
			struct ibv_recv_wr *temp_recv;
			for (int i = 0; i < qp_quantity; i++)
			{
				if (recv_wr_ptr_list[i])
				{
					//adjust the head node
					temp_recv = recv_wr_ptr_list[i];
					recv_wr_ptr_list[i] = recv_wr_ptr_list[i]->next;
					temp_recv->next = NULL;

					//multipost
					PRINT_IN_PINK("before multipost recv qp[%d]\n", i);
					multi_post_recv(res, i, recv_wr_ptr_list[i]);
					PRINT_IN_PINK("after multipost recv qp[%d]\n", i);
				}
			}
			if (recv_wr_ptr_list)
			{
				free(recv_wr_ptr_list);
				recv_wr_ptr_list = NULL;
			}
		}

		size_t sync_temp;
		if (sock_sync(res->ctrl_sock, sizeof(size_t), (void *)&total_polled_qtt, (void *)&sync_temp))
		{
			PRINT_IN_RED("failed to send the recv_quantity from opposite side\n");
			exit(1);
		}
		else
		{
			PRINT_IN_PINK("Successfully send received quantity: %lu\n", total_polled_qtt);
		}

		barrier_sem_inter_intra_nodes();
		return 0;
	}
}

size_t test_bw(struct resources *res)
{
	if (config.server_name)
	{
		size_t total_time = 0;

		size_t warm_up_loop_time = REPEAT / 4;
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			post_send(res, i);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp * (warm_up_loop_time - 1); i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
			post_send(res, temp);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
		}

		size_t ts = get_timestamp();
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			PRINT_IN_GREEN("posting\t%d\t\n", i);
			post_send(res, i);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp * (REPEAT - 1); i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
			if (i % 1000 == 0)
			{
				PRINT_IN_GREEN("polled\t%d\n", i);
			}
			post_send(res, temp);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
		}

		size_t te = get_timestamp();
		total_time = te - ts;
		return total_time;
	}
	else
	{
		// do nothing
	}
}

size_t test_lat(struct resources *res)
{
	if (config.server_name)
	{
		size_t total_time = 0;
		size_t t_post[qp_quantity * mr_per_qp];
		size_t warm_up_loop_time = REPEAT / 4;
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			post_send(res, i);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp * (warm_up_loop_time - 1); i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
			post_send(res, temp);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
		}

		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			PRINT_IN_GREEN("posting\t%d\t\n", i);
			t_post[i] = get_timestamp();
			post_send(res, i);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp * (REPEAT - 1); i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
			total_time += get_timestamp() - t_post[temp];
			if (i % 1000 == 0)
			{
				PRINT_IN_GREEN("polled\t%d\n", i);
			}
			t_post[temp] = get_timestamp();
			post_send(res, temp);
		}
		for (int i = 0; i < qp_quantity * mr_per_qp; i++)
		{
			int temp;
			poll_completion(res, TIMEOUT, &temp);
			total_time += get_timestamp() - t_post[temp];
		}
		return total_time;
	}
	else
	{
		// do nothing
	}
}

void set_repeat()
{
	size_t t_repeat = REPEAT;
	if (1.0 * mr_size * mr_per_qp * proc_quantity >= 10 * 1024 * 1024)
	{
		t_repeat = 10.0 * 10 * 1024 / proc_quantity / (1.0 * qp_quantity * mr_per_qp * mr_size / 1048576.0);
		REPEAT = (t_repeat < REPEAT ? t_repeat : REPEAT);
	}
}
int confirm_mem_req()
{
	barrier_tag = 0;
	int coefficient = 1;
	global_max_poll_quantity = qp_quantity * (mr_per_qp > wr_per_qp ? mr_per_qp : wr_per_qp);

	// multiply by two for send and receive
	if (pingpong_mode)
	{
		global_max_poll_quantity *= 2;
		coefficient = 2;
	}
	if (coefficient * mr_quantity * proc_quantity * ((mr_size + 100 * 1024) / 1024.0 / 1024 / 1024) > LIMIT * 1.0)
	{
		if (proc_index)
		{
			return 1;
		}

		char print_string[4096] = "";
		struct timeval tv;
		gettimeofday(&tv, NULL);
		struct tm *lt = localtime(&(tv.tv_sec));
		strftime(time_string, sizeof(time_string), "%m/%d_%H:%M:%S", lt);
		// another version of printfile
		// versionid
		sprintf(print_string, "\n%s\n", VersionID);
		// date time
		sprintf(print_string + strlen(print_string), "%s\n", time_string);
		// thread bound
		sprintf(print_string + strlen(print_string), "thrd_bnd:\t%u\n", thread_bound);
		// qp num
		sprintf(print_string + strlen(print_string), "qp_qtt:\t%u\n", qp_quantity);
		// mr num
		sprintf(print_string + strlen(print_string), "mr_per_qp\t%u\n", mr_per_qp);
		// mr size
		sprintf(print_string + strlen(print_string), "mr_sz:\t%lu\n", mr_size);
		// msg size
		sprintf(print_string + strlen(print_string), "msg_sz:\t%lu\n", msg_size);
		// thread_quantity
		sprintf(print_string + strlen(print_string), "thrd_qtt:\t%lu\n", thread_quantity);
		// proc_index
		sprintf(print_string + strlen(print_string), "proc_index:\t%d\n", proc_index);
		// proc_quantity
		sprintf(print_string + strlen(print_string), "proc_qtt:\t%d\n", proc_quantity);
		// time_len
		sprintf(print_string + strlen(print_string), "time_len:\t%lu\n", test_time_length);
		// repeattime
		sprintf(print_string + strlen(print_string), "rpt:\t%lu\n", REPEAT);
		// PATHMTU
		sprintf(print_string + strlen(print_string), "pmtu:\t%u\n", path_mtu);
		PRINT_IN_RED("%s", print_string);
		PRINT_IN_RED("using to much memory\n");
		return 1;
	}
	return 0;
}

void setCPUbound(int number_bound)
{
	number_bound = number_bound + bound_offset;
	number_bound %= 16;
	int f;
	cpu_set_t cpuset;
	pthread_t thread;
	CPU_ZERO(&cpuset);
	thread = pthread_self();
	CPU_SET(number_bound, &cpuset);
	f = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (f)
	{
		printf("\033[1;31m");
		printf("f is %d, failed to set affinity\n", f);
		printf("\033[0m");
		exit(1);
	}

	return;
}
void checkCPUbound(int number_bound)
{
	number_bound = number_bound + bound_offset;

	number_bound %= 16;
	int f;
	cpu_set_t cpuset;
	pthread_t thread;
	CPU_ZERO(&cpuset);
	thread = pthread_self();

	f = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (f)
	{
		printf("\033[1;31m");
		printf("f is %d, failed to get affinity\n", f);
		printf("\033[0m");
		exit(1);
	}

	if (!CPU_ISSET(number_bound, &cpuset))
	{
		printf("\033[1;31m");
		printf("Cpu %d is not set\n", number_bound);
		printf("\033[0m");
		exit(1);
	}
	for (int j = 0; j < CPU_SETSIZE; j++)
	{
		if (j != number_bound && CPU_ISSET(j, &cpuset))
		{
			printf("\033[1;31m");
			printf("CPU %d is set\n", j);
			printf("\033[0m");
			exit(1);
		}
	}
}

int resources_create(struct resources *res)
{
	memset(res, 0, sizeof(res));
	res->data_sock = -1;
	res->ctrl_sock = -1;
	return 0;
}

void config_inline()
{
	max_inline_length = 0;
	if (inline_mode)
	{
		if (qp_type == IBV_QPT_UD && DEVICE_INLINE_LENGTH_UD >= msg_size + 40)
		{
			max_inline_length = msg_size;
		}
		else if (qp_type != IBV_QPT_UD && DEVICE_INLINE_LENGTH >= msg_size)
		{
			max_inline_length = msg_size;
		}
	}
	return;
}

int test_body(void)
{
	if (thread_bound)
	{
		setCPUbound(4);
		checkCPUbound(4);
	}

	// open the file to write the output
	PRINT_IN_PINK("new loop begin\nthis is what I'm doing with\n");
	confirm_mem_req();
	set_repeat();
	config_inline();
	set_recv_id_offset();
	print_info();
	char temp_char;
	resources_create(&res);
	sock_create(&res);
	perror("peer\t");
	PRINT_IN_BLUE("Before the dev open\n");
	open_dev(&res);
	perror("peer\t");
	PRINT_IN_BLUE("After the dev open\tBefore the pd alloc\n");
	alloc_pd(&res);
	perror("peer\t");
	PRINT_IN_BLUE("After the pd alloc\n");

	create_cq(&res);
	perror("peer\t");
	PRINT_IN_BLUE("After the cq creation\n");

	alloc_buf(&res);
	perror("peer\t");
	PRINT_IN_BLUE("After the buf alloc\n");

	alloc_mr(&res);
	PRINT_IN_BLUE("After the mr alloc\n");

	create_qp(&res);
	PRINT_IN_BLUE("After the pd creation\n");

	connect_qp(&res);
	PRINT_IN_BLUE("After the qp connection\n");

	struct ibv_send_wr *bad_wr = NULL;
	/* prepare the scatter/gather entry */
	int qp_num = 0;
	// PRINT("preparing wrs\n");
	prepare_wrs(&res);
	PRINT_IN_BLUE("After the wr preparation\n");

	size_t total_time;
	if (pingpong_mode)
	{
		total_time = test_core_pingpong(&res);
	}
	else
	{
		total_time = test_core(&res);
	}
	PRINT_IN_BLUE("After the test core\n");

	if (sock_sync(res.ctrl_sock, 1, "R", &temp_char))
	{
		PRINT_IN_RED("error Sync before destroy\n");
		exit(1);
	}
	else
	{
		PRINT_IN_YELLOW("successfully Sync before destroy\n");
	}
	PRINT_IN_YELLOW("starting resources destroy\n");

	resources_destroy(&res);
	PRINT_IN_YELLOW("after resources destroy\n");
	print_final_result(total_time);
	PRINT_IN_YELLOW("after print final result\n");
	destroy_sock(&res);
	if (thread_bound)
	{
		checkCPUbound(4);
	}
	return 0;
}

void set_global_attr()
{
	//pagesize
	if (!strcmp(page_size_str, "4KB"))
	{
		page_size = 4 * 1024;
	}
	else if (!strcmp(page_size_str, "2MB"))
	{
		page_size = 2 * 1024 * 1024;
	}
	else if (!strcmp(page_size_str, "1GB"))
	{
		page_size = 1024 * 1024 * 1024;
	}

	// config port number
	config.tcp_ctrl_port += port_offset;
	config.tcp_data_port += port_offset;
	// set opcode
	if (pingpong_mode)
	{
		if (use_data_gram)
		{
			opcode = IBV_WR_SEND_WITH_IMM;
		}
		else
		{
			opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
		}
	}
	else
	{
		if (!strcmp(test_subject, "bw") || !strcmp(test_subject, "lat"))
		{
			opcode = IBV_WR_RDMA_WRITE;
		}
		else if (!strcmp(test_subject, "msg_rate"))
		{
			if (use_data_gram)
			{
				opcode = IBV_WR_SEND_WITH_IMM;
			}
			else
			{
				opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
			}
		}
		else
		{
			PRINT_IN_RED("set opcode error set global attr 1\n");
			exit(1);
		}
	}
	// set qp type
	if (!strcmp(test_subject, "bw") || !strcmp(test_subject, "lat"))
	{
		if (use_data_gram)
		{
			qp_type = IBV_QPT_UD;
		}
		else
		{
			qp_type = IBV_QPT_RC;
		}
	}
	else if (!strcmp(test_subject, "msg_rate"))
	{
		if (use_data_gram)
		{
			qp_type = IBV_QPT_UD;
		}
		else
		{
			qp_type = IBV_QPT_UC;
		}
	}
	else
	{
		PRINT_IN_RED("set qp type error set global attr 2\n");
		exit(1);
	}
}

void set_recv_id_offset()
{
	recv_id_offset = qp_quantity * mr_per_qp;
}

int check_QP_state(struct resources *res, char *printstr)
{
	PRINT_IN_RED("checking QP state\n");
	struct ibv_device_attr d_attr;
	struct ibv_qp_attr attr;
	int attr_mask = IBV_QP_STATE;
	struct ibv_qp_init_attr init_attr;
	int flag = 0;
	size_t t1 = 0;
	for (int j = 0; j < qp_quantity; j++)
	{
		int i = j;
		ibv_query_qp(res->qp[i], &attr, attr_mask, &init_attr);
		if (attr.qp_state != 3)
		{
			flag = 1;
			t1 = get_timestamp();
			PRINT_IN_RED("QP[%d] state is %d\n", i, attr.qp_state);
		}
	}
	if (flag)
	{
		size_t t2 = 0;
		//print_pingpong_count();

		do
		{
			t2 = get_timestamp();
			flag = 0;
			for (int i = 0; i < qp_quantity; i++)
			{
				ibv_query_qp(res->qp[i], &attr, attr_mask, &init_attr);
				// PRINT_IN_PINK("QP[%d] state is %d\n", i, attr.qp_state);
				if (attr.qp_state != 3)
				{
					flag = 1;
				}
			}
			if (!flag)
			{
				return 0;
			}
		} while (t2 - t1 < 200000);
		PRINT_IN_RED("%s: QP FOUND IN ERROR STATE\n", printstr);
		return 1;
	}
	else
	{
		PRINT_IN_PINK("QP status is OK\n");
		return 0;
	}
}
void print_pingpong_count()
{
	size_t total = 0;
	for (int i = 0; i < qp_quantity * mr_per_qp; i++)
	{
		//PRINT_IN_PINK("[%d]:%6lu\n", i, mr_pingpong_count[i]);
		total += mr_pingpong_count[i];
	}
	PRINT_IN_PINK("total pingpong count %6lu\n", total);
}

void barrier_old(char *ready, char *start)
{
	PRINT_IN_PINK("entering barrier tag: %d\n", barrier_tag);
	// sync between oneside processes
	sem_t *sem_ready = sem_open_existing(sem_name_test_ready);
	sem_t *sem_start = sem_open_existing(sem_name_test_start);
	if (proc_index == 0)
	{
		// ready
		PRINT_IN_YELLOW("proc 0 waiting for ready\n");
		for (int i = 0; i < proc_quantity - 1; i++)
		{
			sem_wait(sem_ready);
		}
		sem_close(sem_ready);
		PRINT_IN_YELLOW("proc 0 received ready\n");
		// process 0 start sync between server and client

		char tempc;

		sock_sync(res.ctrl_sock, 1, "r", &tempc);
		// process 0 ends sync between server and client
		if (config.server_name)
		{ // client should give time to server to post recv
		}
		PRINT_IN_YELLOW("proc 0 send start\n");
		// start
		for (int i = 0; i < proc_quantity - 1; i++)
		{
			sem_post(sem_start);
		}
		PRINT_IN_YELLOW("proc 0 start already sent\n");

		sem_close(sem_start);
	}
	else
	{ // ready
		PRINT_IN_YELLOW("sending ready\n");
		sem_post(sem_ready);
		sem_close(sem_ready);
		PRINT_IN_YELLOW("ready already sent\n");
		// start
		PRINT_IN_YELLOW("waiting for start\n");
		sem_wait(sem_start);
		sem_close(sem_start);
		PRINT_IN_YELLOW("received start\n");
	}
	PRINT_IN_PINK("exiting barrier tag: %d\n", barrier_tag);
	barrier_tag++;
}

void barrier_sem_inter_intra_nodes()
{
	sem_t *sem_shmem = sem_open_existing(sem_name_shmem_access);
	PRINT_IN_PINK("entering barrier tag: %d\n", barrier_tag);
	// sync between oneside processes
	if (proc_index == 0)
	{
		// ready
		PRINT_IN_YELLOW("proc 0 waiting for ready\n");
		while (pshemem_struct->ready != proc_quantity - 1)
		{
			//PRINT_IN_PINK("ready is %d\n",pshemem_struct->ready);
		}
		sem_wait(sem_shmem);
		pshemem_struct->ready = 0;
		sem_post(sem_shmem);

		PRINT_IN_YELLOW("proc 0 received ready\n");
		// process 0 start sync between server and client

		char tempc;

		sock_sync(res.ctrl_sock, 1, &sync_confirm_index, &tempc);
		if (sync_confirm_index != tempc)
		{
			PRINT_IN_RED("sync indices are different %d %d\n", (int)tempc, (int)sync_confirm_index);
			exit(1);
		}
		else
		{
			PRINT_IN_PINK("sync indexes are the same %d %d\n", (int)tempc, (int)sync_confirm_index);
		}
		sync_confirm_index++;
		// process 0 ends sync between server and client
		if (config.server_name)
		{ // client should give time to server to post recv
		}
		PRINT_IN_YELLOW("proc 0 send start\n");
		// start
		sem_wait(sem_shmem);
		if (pshemem_struct->start == target_start_flag)
		{
			PRINT_IN_RED("!bug\n");
			exit(1);
		}
		pshemem_struct->start = target_start_flag;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		target_start_flag = !target_start_flag;
		PRINT_IN_YELLOW("proc 0 start already sent\n");
	}
	else
	{ // ready
		PRINT_IN_YELLOW("sending ready\n");
		sem_wait(sem_shmem);
		pshemem_struct->ready++;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		PRINT_IN_YELLOW("ready already sent\n");
		// start
		PRINT_IN_YELLOW("waiting for start\n");
		while (pshemem_struct->start != target_start_flag)
		{
			//PRINT_IN_PINK("start is %d target is %d\n",pshemem_struct->start,target_start_flag);
		}

		target_start_flag = !target_start_flag;
		PRINT_IN_YELLOW("received start\n");
	}
	PRINT_IN_PINK("exiting barrier tag: %d\n", barrier_tag);
	barrier_tag++;
}

void barrier_sem_intra_nodes()
{
	sem_t *sem_shmem = sem_open_existing(sem_name_shmem_access);
	PRINT_IN_PINK("entering barrier tag: %d\n", barrier_tag);
	// sync between oneside processes
	if (proc_index == 0)
	{
		// ready
		PRINT_IN_YELLOW("proc 0 waiting for ready\n");
		while (pshemem_struct->ready != proc_quantity - 1)
		{
			//PRINT_IN_PINK("ready is %d\n",pshemem_struct->ready);
		}
		sem_wait(sem_shmem);
		pshemem_struct->ready = 0;
		sem_post(sem_shmem);

		PRINT_IN_YELLOW("proc 0 received ready\n");
		// process 0 start sync between server and client

		sync_confirm_index++;
		// process 0 ends sync between server and client
		if (config.server_name)
		{ // client should give time to server to post recv
		}
		PRINT_IN_YELLOW("proc 0 send start\n");
		// start
		sem_wait(sem_shmem);
		if (pshemem_struct->start == target_start_flag)
		{
			PRINT_IN_RED("!bug\n");
			exit(1);
		}
		pshemem_struct->start = target_start_flag;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		target_start_flag = !target_start_flag;
		PRINT_IN_YELLOW("proc 0 start already sent\n");
	}
	else
	{ // ready
		PRINT_IN_YELLOW("sending ready\n");
		sem_wait(sem_shmem);
		pshemem_struct->ready++;
		sem_post(sem_shmem);
		sem_close(sem_shmem);
		PRINT_IN_YELLOW("ready already sent\n");
		// start
		PRINT_IN_YELLOW("waiting for start\n");
		while (pshemem_struct->start != target_start_flag)
		{
			//PRINT_IN_PINK("start is %d target is %d\n",pshemem_struct->start,target_start_flag);
		}

		target_start_flag = !target_start_flag;
		PRINT_IN_YELLOW("received start\n");
	}
	PRINT_IN_PINK("exiting barrier tag: %d\n", barrier_tag);
	barrier_tag++;
}

sem_t *clear_and_open_sem(char *sem_name)
{
	sem_t *sem = sem_open(sem_name, O_CREAT, 0600, 0);
	sem_close(sem);
	sem_unlink(sem_name);
	PRINT_IN_PINK("sem %s cleared\n", sem_name);
	sem = sem_open(sem_name, O_CREAT | O_EXCL, 0600, 0);
	PRINT_IN_PINK("sem %s opened\n", sem_name);
	return sem;
}
sem_t *sem_open_existing(char *sem_name)
{
	PRINT_IN_YELLOW("opening existing sem\n");
	sem_t *sem = sem_open(sem_name, O_EXCL);
	PRINT_IN_YELLOW("existing sem opened\n");
	return sem;
}

void *create_shared_memory()
{
	param_shmem_fd = open("./file.txt", O_RDWR | O_CREAT | O_TRUNC);
	if (param_shmem_fd < 0)
	{
		perror("fopen");
	}
	else
	{
		PRINT_IN_YELLOW("param_shmem_fd is %d\n", param_shmem_fd);
	}

	struct shmem_struct content;
	content.start = init_start_flag;
	content.ready = 0;
	content.error_flag = 0;
	write(param_shmem_fd, (void *)&content, sizeof(struct shmem_struct));
	struct stat fd_stat_ubf;
	if (fstat(param_shmem_fd, &fd_stat_ubf))
	{
		perror("fstat");
	}
	PRINT_IN_YELLOW("size = %lu\n", fd_stat_ubf.st_size);
	// Our memory buffer will be readable and writable:
	int protection = PROT_READ | PROT_WRITE;

	// The buffer will be shared (meaning other processes can access it), but
	// anonymous (meaning third-party processes cannot obtain an address for it),
	// so only this process and its children will be able to use it:
	int visibility = MAP_SHARED;

	// The remaining parameters to `mmap()` are not important for this use case,
	// but the manpage for `mmap` explains their purpose.
	void *ret = mmap(NULL, fd_stat_ubf.st_size, protection, visibility, param_shmem_fd, 0);
	close(param_shmem_fd);
	return ret;
}
void *open_shared_memory()
{
	param_shmem_fd = open("./file.txt", O_RDWR);
	if (param_shmem_fd < 0)
	{
		perror("fopen");
	}
	else
	{
		PRINT_IN_YELLOW("param_shmem_fd is %d\n", param_shmem_fd);
	}

	struct shmem_struct content;

	struct stat fd_stat_ubf;
	if (fstat(param_shmem_fd, &fd_stat_ubf))
	{
		perror("fstat");
	}
	PRINT_IN_BLUE("size = %lu\n", fd_stat_ubf.st_size);
	// Our memory buffer will be readable and writable:
	int protection = PROT_READ | PROT_WRITE;

	// The buffer will be shared (meaning other processes can access it), but
	// anonymous (meaning third-party processes cannot obtain an address for it),
	// so only this process and its children will be able to use it:
	int visibility = MAP_SHARED;

	// The remaining parameters to `mmap()` are not important for this use case,
	// but the manpage for `mmap` explains their purpose.
	void *ret = mmap(NULL, fd_stat_ubf.st_size, protection, visibility, param_shmem_fd, 0);
	close(param_shmem_fd);
	return ret;
}

void set_verID_subj_ps(char *verid, char *sub, char *psstr)
{
	if (strlen(verid) >= 50 || strlen(sub) >= 20 || strlen(psstr) >= 10)
	{
		PRINT_IN_RED("too long string\n");
		exit(1);
	}
	memcpy(VersionID, verid, strlen(verid) + 1);
	memcpy(test_subject, sub, strlen(sub) + 1);
	memcpy(page_size_str, psstr, strlen(psstr) + 1);
}