#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>
#define BAMBU_DYNAMIC
#include "slic3r/GUI/Printer/BambuTunnel.h"

struct BambuLib lib;

static void _log(void *ctx, int lvl, const char* msg) {
	fprintf(stderr, "bambu: log lvl %d: %s\n", lvl, msg);
	lib.Bambu_FreeLogMsg(msg);
}

Bambu_Tunnel tnl = NULL;

static void _cleanup_shm() {
	if (tnl) {
		lib.Bambu_Close(tnl);
		lib.Bambu_Destroy(tnl);
		tnl = NULL;
	}
}

static void _sigint(int sig) {
	fprintf(stderr, "received signal %d, gracefully shutting down\n", sig);
	exit(0);
}

int main(int argc, char **argv) {
	const char *path;
	void *bambusource;
	
	signal(SIGINT, _sigint);
	signal(SIGTERM, _sigint);
	
	bambusource = dlopen("libBambuSource.so", RTLD_NOW);
	if (!bambusource) {
		fprintf(stderr, "dlopen(\"libBambuSource.so\"): %s\n", dlerror());
		return 1;
	}
#define LOAD(func) \
	lib.func = dlsym(bambusource, #func); \
	if (!lib.func) { \
		fprintf(stderr, "dlsym(..., " #func "): symbol missing\n"); \
		return 1; \
	}
	LOAD(Bambu_Create);
	LOAD(Bambu_SetLogger);
	LOAD(Bambu_FreeLogMsg);
	LOAD(Bambu_Open);
	LOAD(Bambu_StartStream);
	LOAD(Bambu_Close);
	LOAD(Bambu_Destroy);
	LOAD(Bambu_ReadSample);	

	if (argc == 2) {
		path = argv[1];
	} else {
		path = "";
	}
	
	if (lib.Bambu_Create(&tnl, path) != Bambu_success) {
		fprintf(stderr, "failed to bambu_create\n");
		return 1;
	}
	
	lib.Bambu_SetLogger(tnl, _log, 0);
	if (lib.Bambu_Open(tnl) != Bambu_success) {
		fprintf(stderr, "failed to bambu_open path\n");
	}
	atexit(_cleanup_shm);

	int rv;
	while ((rv = lib.Bambu_StartStream(tnl, 1 /* video */)) == Bambu_would_block) {
		usleep(100000);
	}
	if (rv != Bambu_success) {
		fprintf(stderr, "failed to start stream\n");
		lib.Bambu_Close(tnl);
		lib.Bambu_Destroy(tnl);
		tnl = NULL;
		return 3;
	}
	
	while (1) {
		struct Bambu_Sample sample;
		while ((rv = lib.Bambu_ReadSample(tnl, &sample)) == Bambu_success) {
			if (write(1, sample.buffer, sample.size) < sample.size) {
				rv = -1;
				break;
			}
		}
		if (rv != Bambu_would_block) {
			break;
		}
		usleep(100000);
	}
	
	fprintf(stderr, "bambu exited with rv %d\n", rv);
	lib.Bambu_Close(tnl);
	lib.Bambu_Destroy(tnl);
	tnl = NULL;
	return 0;
}
