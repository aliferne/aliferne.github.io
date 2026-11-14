CC := cc
DYN_FLAGS := -shared -fPIC
LDFLAGS := -ldl -lraylib

.PHONY: prepare dyn_no_rb_call dyn_with_rb_call dyn_no_as_need dyn_to_gbl static all

all: prepare dyn_no_rb_main dyn_with_rb_main dyn_no_as_need_main dyn_to_gbl_main sta_main

prepare:
		$(CC) add.c $(DYN_FLAGS) -o add.so && $(CC) dyn.c $(DYN_FLAGS) -o dyn.so

dyn_no_rb_main: prepare
		$(CC) main.c $(LDFLAGS) -DDYN_COMPILE -o $@

dyn_with_rb_main: prepare
		$(CC) main.c $(LDFLAGS) -DDYN_COMPILE -DCALL_RAYLIB_FUNC -o $@

dyn_no_as_need_main: prepare
		$(CC) main.c -DDYN_COMPILE -DNO_AS_NEEDED -o $@ -Wl,--no-as-needed $(LDFLAGS)

dyn_to_gbl_main: prepare
		$(CC) main.c $(LDFLAGS) -DDYN_COMPILE -DADD_TO_GLOBAL -o $@

sta_main: main.c static.c
		$(CC) $^ -o sta_main 

dyn_no_rb_call: dyn_no_rb_main
		@echo "------------------------ dyn_no_rb_call ------------------------"
		@./dyn_no_rb_main

dyn_with_rb_call: dyn_with_rb_main
		@echo "------------------------ dyn_with_rb_call ------------------------"
		@./dyn_with_rb_main

dyn_no_as_need: dyn_no_as_need_main
		@echo "------------------------ dyn_no_as_need ------------------------"
		@./dyn_no_as_need_main

dyn_to_gbl: dyn_to_gbl_main
		@echo "------------------------ dyn_to_gbl ------------------------"
		@./dyn_to_gbl_main

static: sta_main
		@echo "------------------------ sta_main ------------------------"
		@./sta_main
