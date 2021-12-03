#include "QP_stat_MP.h"

// c code are the same on both sides
// no need for different config files, dev and ip are configured by input
//
int main_QPC(int argc, char *argv[]);

size_t qp_quantity_array[] = {1};
size_t mr_per_qp_array[] = {1};
size_t msg_size_array[] = {8};
size_t minimum_mr_size = 4;

int set_attribute_for_once(int argc, char *argv[])
{
	share_buf_between_mr = 0;
	TRY_TIMEOUT = 500;
	use_data_gram = 0;
	pingpong_mode = 0;
	share_mr_between_qp = 0;
	test_time_length = 4000000;
	log_verbose_level = 0;
	inline_mode = 1;
	path_mtu = 5;
	// char file_name[50] = "ud_shrmr_inlineoff_mrqpnum_8_64size_";
	char file_name[50] = "QPC_TEST_clk_";

	char *subject_str = "msg_rate";
	int *proc_index_input;
	while (1)
	{
		int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			{"proc_ind", required_argument, NULL, 1},
			{"proc_qtt", required_argument, NULL, 2},
			{"c", 1, NULL, 3},
			{"pingpong", 0, NULL, 4},
			{"v", 2, NULL, 5},
			{"inline", 0, NULL, 6},
			{"mtu", 1, NULL, 7},
			{"shr_mr", 0, NULL, 8},
			{"time_length", 1, NULL, 9},
			{"sub", 1, NULL, 10},
			{"dev_name", 1, NULL, 12},
			{0, 0, 0, 0}};
		// getopt_long(argc,argv,"");
		int c = getopt_long(argc, argv, "d:c:v:p:", long_options, NULL);
		printf("c is %d char %c\n", c, c);
		if (c == -1)
			break;
		switch (c)
		{
		case 1:
			proc_index = (int)strtoimax(optarg, NULL, 0);
			break;
		case 2:
			proc_quantity = (int)strtoimax(optarg, NULL, 0);
			break;
		case 'c':
		case 3:
			printf("%s strlen is %lu\n", optarg, strlen(optarg));
			printf("strlen of UD %lu\n", strlen("UD"));
			if (!strcmp("UD", optarg))
			{
				printf("set UD\n");
				use_data_gram = 1;
			}
			break;
		case 4:
			pingpong_mode = 1;
			break;
		case 'v':
		case 5:
			log_verbose_level = strtol(optarg, NULL, 0);
			break;
		case 6:
			inline_mode = 1;
			break;
		case 7:
			path_mtu = strtol(optarg, NULL, 0);
			break;
		case 8:
			share_mr_between_qp = 1;
			break;
		case 9:
			test_time_length = strtoul(optarg, NULL, 0);
			break;
		case 10:
			strcpy(file_name, optarg);
			break;
		case 'd':
		case 12:
			config.dev_name = optarg;
			break;
		case 'p':
		case 13:
			config.tcp_data_port = (unsigned int)strtoumax(optarg, NULL, 0);
			break;
		default:
			usage(argv[0]);
			printf("invalid arguments\n");
			return 1;
		}
	}
	if (optind == argc - 1)
	{
		config.server_name = argv[optind];
		PRINT_IN_PINK("servername : %s\n", config.server_name);
	}
	config.tcp_ctrl_port = config.tcp_data_port + 200;
	char proc_quantity_str[4] = {0, 0, 0, 0};
	sprintf(proc_quantity_str, "%d", proc_quantity);
	strcat(file_name, proc_quantity_str);
	PRINT_FOR_ROOT("file name is :%s\n", file_name);

	// char file_name[50] = "msg_rate_QP_MR_V1_test_2_Proc_qtt";

	set_verID_subj_ps(file_name, subject_str, "4KB");
	for (int i = 0; i < argc; i++)
	{
		PRINT_IN_PINK("argv[%d]:%s\n", i, argv[i]);
	}
	port_offset = proc_index;

	return 0;
}
static void usage(const char *argv0)
{
	PRINT("Usage:\n");
	PRINT(" %s start a server and wait for connection\n", argv0);
	PRINT(" %s <host> connect to server at <host>\n", argv0);
	PRINT("\n");
	PRINT("Options:\n");
	PRINT(" -p, --port <port> listen on/connect to port <port> (default 18515)\n");
	PRINT(" -d, --ib-dev <dev> use IB device <dev> (default first device found)\n");
	PRINT(" -i, --ib-port <port> use port <port> of IB device (default 1)\n");
	PRINT(" -g, --gid_idx <git index> gid index to be used in GRH "
		  "(default not used)\n");
	PRINT(" -s, --size <size> use size <size> for transport data size\n");
	PRINT(" -l, --loop <loop number> use <loop number> for test loop number\n");
}
int test_body_multi_thread(void)
{
	error_flag = 0;
	if (!proc_index)
	{
		pshemem_struct->error_flag = 0;
	}
	barrier_sem_intra_nodes();
	if (thread_bound)
	{
		setCPUbound(proc_index);
		checkCPUbound(proc_index);
	}

	// open the file to write the output
	PRINT_IN_PINK("new loop begin\nthis is what I'm doing with\n");
	if (confirm_mem_req())
	{
		return 0;
	}
	set_repeat();
	config_inline();
	if (inline_mode && !max_inline_length)
	{
		if (!proc_index)
		{
			PRINT_IN_RED("INLINE NOT AVAILABLE\n");
		}
		return 0;
	}
	set_recv_id_offset();

	print_info();

	char temp_char;
	PRINT_IN_YELLOW("before the res create\n");

	resources_create(&res);
	PRINT_IN_YELLOW("after the res create\n");
	sock_create(&res);
	PRINT_IN_YELLOW("Before the dev open\n");
	PRINT_FOR_ROOT("dev open\n");
	open_dev(&res);
	PRINT_IN_YELLOW("After the dev open\tBefore the pd alloc\n");
	PRINT_FOR_ROOT("pd\n");
	alloc_pd(&res);
	PRINT_IN_YELLOW("After the pd alloc\n");
	PRINT_FOR_ROOT("cq\n");
	create_cq(&res);
	if (end_this_loop)
	{
		end_this_loop = 0;
		goto restore_env;
	}
	PRINT_IN_YELLOW("After the cq creation\n");
	PRINT_FOR_ROOT("buf\n");

	alloc_buf(&res);
	PRINT_IN_YELLOW("After the buf alloc\n");
	PRINT_FOR_ROOT("mr\n");
	alloc_mr(&res);

	PRINT_IN_YELLOW("After the mr alloc\n");
	PRINT_FOR_ROOT("create qp\n");
	create_qp(&res);

	PRINT_IN_YELLOW("After the pd creation\n");
	PRINT_FOR_ROOT("connect qp\n");
	log_verbose_level += 0;
	connect_qp(&res);

	PRINT_IN_YELLOW("After the qp connection\n");
	log_verbose_level -= 0;

	struct ibv_send_wr *bad_wr = NULL;
	/* prepare the scatter/gather entry */
	int qp_num = 0;
	// PRINT("preparing wrs\n");
	PRINT_FOR_ROOT("wr\n");

	prepare_wrs(&res);

	PRINT_IN_YELLOW("After the wr preparation\n");

	size_t total_time;
	size_t total_xfer_quantity;
	PRINT_FOR_ROOT("test begin\n");
	if (pingpong_mode)
	{
		test_core_pingpong(&res);
	}
	else
	{
		total_time = test_core(&res);
	}
	PRINT_IN_PINK("After the test core\n");
	PRINT_FOR_ROOT("test end\n");
	print_final_result_fixed_time_length();

restore_env:
	if (sock_sync(res.ctrl_sock, 1, "R", &temp_char))
	{
		PRINT_IN_RED("error Sync before destory\n");
	}
	else
	{
		PRINT_IN_YELLOW("successfully Sync before destory\n");
	}
	PRINT_IN_YELLOW("starting resources destroy\n");

	resources_destroy(&res);

	PRINT_IN_YELLOW("after resources destroy\n");

	PRINT_IN_YELLOW("after print final result\n");
	barrier_sem_inter_intra_nodes();

	destroy_sock(&res);
	if (thread_bound)
	{
		checkCPUbound(proc_index);
	}
	return 0;
}
int main(int argc, char *argv[])
{
	main_QPC(argc, argv);
	return 0;
}

int main_QPC(int argc, char *argv[])
{
	// QPC
	set_attribute_for_once(argc, argv);
	share_buf_between_mr = 0;
	use_data_gram = 1;
	pingpong_mode = 0;
	share_mr_between_qp = 1;
	test_time_length = 4000000;
	// log_verbose_level = 0;
	inline_mode = 1;
	path_mtu = 5;
	pshemem_struct = open_shared_memory();
	int jump = 0;
	wr_per_qp = 0;
	set_global_attr();
	qp_quantity = 1;
	if (proc_quantity != 1)
	{
		return 0;
	}
	for (; qp_quantity <= 65536 / proc_quantity; qp_quantity *= 2) // 5
	{
		// mr_per_qp = 1;
		// for (wr_per_qp = 256 / proc_quantity; wr_per_qp <=  2048/ proc_quantity; wr_per_qp *= 2) //5
		mr_per_qp = 1;
		for (; mr_per_qp <= 1; mr_per_qp *= 2) // 5
		{

			for (int base_msg_size = 1; base_msg_size <= 1; base_msg_size *= 8) // 21
			{
				if (base_msg_size < 1)
				{
					continue;
				}
				for (int i = 0; i <= 0; i++)
				{
					msg_size = base_msg_size + i;
					if (use_data_gram && base_msg_size == 4096)
					{
						msg_size = base_msg_size + i - 40;
					}
					if (qp_type == IBV_QPT_UD)
					{

						mr_size = msg_size + 40;
					}
					else
					{
						mr_size = (msg_size > minimum_mr_size ? msg_size : minimum_mr_size);
					}
					for (path_mtu = 5; path_mtu <= 5; path_mtu += 4) // 2
					{
						for (thread_bound = 1; thread_bound <= 1; thread_bound++) // 2
						{
							for (inline_mode = 1; inline_mode <= 1; inline_mode++) // 2
							{
								if (unsignaled)
								{
									inline_mode = 1;
								}
								int i = 1;
								do // 1
								{
									sync_confirm_index++;
									PRINT_IN_GREEN("entering test body\n");

									if (share_mr_between_qp)
									{
										mr_quantity = mr_per_qp;
									}
									else
									{
										mr_quantity = mr_per_qp * qp_quantity;
									}
									for (int t = 1; t <= 1; t++)
									{

										for (int tt = 0; tt < 2; tt++)
										{
											test_body_multi_thread();
										}
									}
								} while (i++ < 0);
								if (unsignaled)
								{
									break;
								}
							}
						}
					}
				}
			}
		}
	}
	return 0;
}

int main_tmp(int argc, char *argv[])
{
	set_attribute_for_once(argc, argv);
	pshemem_struct = open_shared_memory();
	int jump = 0;
	wr_per_qp = 0;
	set_global_attr();
	qp_quantity = 8192;
	for (; qp_quantity <= 8192 / proc_quantity;) // 5
	{
		// mr_per_qp = 1;
		// for (wr_per_qp = 256 / proc_quantity; wr_per_qp <=  2048/ proc_quantity; wr_per_qp *= 2) //5
		mr_per_qp = 1;
		if (qp_quantity == 512)
		{
			mr_per_qp = 4;
		}
		for (; mr_per_qp <= 8192 / proc_quantity; mr_per_qp *= 2) // 5
		{
			for (int base_msg_size = 8; base_msg_size <= 64; base_msg_size *= 8) // 21
			{
				if (base_msg_size < 1)
				{
					continue;
				}
				for (int i = 0; i <= 0; i++)
				{
					msg_size = base_msg_size + i;
					if (use_data_gram && base_msg_size == 4096)
					{
						msg_size = base_msg_size + i - 40;
					}
					if (qp_type == IBV_QPT_UD)
					{
						if (config.server_name)
						{
							mr_size = msg_size + 40;
						}
						else
						{
							mr_size = msg_size + 40;
						}
					}
					else
					{
						mr_size = (msg_size > minimum_mr_size ? msg_size : minimum_mr_size);
					}
					for (path_mtu = 5; path_mtu <= 5; path_mtu += 4) // 2
					{
						for (thread_bound = 0; thread_bound < 1; thread_bound++) // 2
						{
							for (inline_mode = 0; inline_mode <= 0; inline_mode++) // 2
							{
								if (unsignaled)
								{
									inline_mode = 1;
								}
								int i = 1;
								do // 1
								{
									sync_confirm_index++;
									PRINT_IN_GREEN("entering test body\n");

									if (share_mr_between_qp)
									{
										mr_quantity = mr_per_qp;
									}
									else
									{
										mr_quantity = mr_per_qp * qp_quantity;
									}
									test_body_multi_thread();
									wr_per_qp = mr_per_qp;
									mr_per_qp = 1;
									if (share_mr_between_qp)
									{
										mr_quantity = mr_per_qp;
									}
									else
									{
										mr_quantity = mr_per_qp * qp_quantity;
									}
									test_body_multi_thread();
									mr_per_qp = wr_per_qp;
									wr_per_qp = 0;
								} while (i++ < 1);
								if (unsignaled)
								{
									break;
								}
							}
						}
					}
				}
			}
		}
		if (qp_quantity < 128)
		{
			qp_quantity *= 2;
		}
		else if (qp_quantity < 1024)
		{
			qp_quantity += 128;
		}
		else
		{
			qp_quantity += 1024;
		}
	}
	return 0;
}
