#include "QP_stat_MP.h"

int main(void)
{

	log_verbose_level = 1;
	proc_index = -111;
	PRINT_IN_PINK("sem init begins\n");
	sem_t *sem;
	sem = clear_and_open_sem(sem_name_test_ready);
	sem_close(sem);

	sem = clear_and_open_sem(sem_name_test_start);
	sem_close(sem);

	sem = clear_and_open_sem(sem_name_ready_2);
	sem_close(sem);


	sem = clear_and_open_sem(sem_name_start_2);
	sem_close(sem);


	sem = clear_and_open_sem(sem_name_shmem_access);
	sem_post(sem);
	sem_close(sem);
	
	PRINT_IN_PINK("sem init ends\n");

	create_shared_memory();


	return 0;
}