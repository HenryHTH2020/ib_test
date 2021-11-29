all: tV1_MP tsync_res_init tsync_res_destroy tpingpong
-include *.d 



tV1_MP: V1_MP.c QP_imple_MP.c
	gcc V1_MP.c QP_imple_MP.c -MMD -o V1_MP -g -libverbs -lpthread

tsync_res_init: sync_res_init.c QP_imple_MP.c 
	gcc sync_res_init.c QP_imple_MP.c -MMD -o exe_sync_res_init -g -libverbs -lpthread

tsync_res_destroy: sync_res_destroy.c QP_imple_MP.c
	gcc sync_res_destroy.c QP_imple_MP.c -MMD -o exe_sync_res_destroy -g -libverbs -lpthread

tpingpong: pingpong.c ud_test_demo.c
	gcc pingpong.c ud_test_demo.c -MMD -o udtest -g -libverbs -lpthread



clean:
	rm udtest V1_MP sync_res_destroy sync_res_init