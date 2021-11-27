#include "QP_stat_MP.h"

int main(void)
{
	log_verbose_level = 1;
	proc_index = -111;

	PRINT_IN_PINK("sync_res_destroy begin\n");
	sem_t *sem = clear_and_open_sem(sem_name_test_ready);
	sem_close(sem);
	sem_unlink(sem_name_test_ready);

	sem = clear_and_open_sem(sem_name_test_start);
	sem_close(sem);
	sem_unlink(sem_name_test_start);

	sem = clear_and_open_sem(sem_name_ready_2);
	sem_close(sem);
	sem_unlink(sem_name_test_start);

	sem = clear_and_open_sem(sem_name_start_2);
	sem_close(sem);
	sem_unlink(sem_name_test_start);

	sem = clear_and_open_sem(sem_name_shmem_access);
	sem_close(sem);
	sem_unlink(sem_name_shmem_access);
	PRINT_IN_PINK("sync_res_destroy ends\n");
	return 0;
}