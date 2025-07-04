#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#define KERNEL_SU_OPTION 0xDEADBEEF

#define CMD_SUSFS_SHOW_VERSION 0x555e1
#define CMD_SUSFS_SHOW_ENABLED_FEATURES 0x555e2
#define CMD_SUSFS_SHOW_VARIANT 0x555e3
#define CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE 0x555e4
#define CMD_SUSFS_IS_SUS_SU_READY 0x555f0
#define CMD_SUSFS_SUS_SU 0x60000

#define SUS_SU_DISABLED 0
#define SUS_SU_WITH_HOOKS 2

struct st_sus_su {
	int mode;
};

int enable_sus_su(int last_working_mode, int target_working_mode) {
	struct st_sus_su info;
	int error = -1;

	if (target_working_mode == SUS_SU_WITH_HOOKS) {
		info.mode = SUS_SU_WITH_HOOKS;
		prctl(KERNEL_SU_OPTION, CMD_SUSFS_SUS_SU, &info, NULL, &error);
		if (error) {
			if (error == 1) {
			} else if (error == 2) {
			}
			return error;
		}
        printf("[+] sus_su mode 2 is enabled\n");
	} else if (target_working_mode == SUS_SU_DISABLED) {
		info.mode = SUS_SU_DISABLED;
		prctl(KERNEL_SU_OPTION, CMD_SUSFS_SUS_SU, &info, NULL, &error);
		if (error) {
			if (error == 1) {
			}
			return error;
		}
        printf("[+] sus_su mode 0 is enabled\n");
	} else {
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[]) {
    int error = -1;
    char support[16];
    char version[16];
    char variant[16];
	


    // Check for arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <support|version|variant|features|sus_su <0|2|mode>|>\n", argv[0]);
        return 1;
    }
        
    if (strcmp(argv[1], "version") == 0) {
        prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_VERSION, version, NULL, &error);
        if (!error) {
            printf("%s\n", version);
        } else {
            printf("Invalid\n");
        }
    } else if (strcmp(argv[1], "variant") == 0) {
        prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_VARIANT, variant, NULL, &error);
        if (!error) {
            printf("%s\n", variant);
        } else {
            printf("Invalid\n");
        }
	} else if (strcmp(argv[1], "features") == 0) {
		char *enabled_features;
        size_t bufsize = getpagesize() * 2;
        enabled_features = (char *)malloc(bufsize);
        if (!enabled_features) {
            perror("malloc");
            return -ENOMEM;
        }
        prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_ENABLED_FEATURES, enabled_features, bufsize, &error);
        if (!error) {
            printf("%s", enabled_features);
        } else {
            printf("Invalid\n");
        }
	} else if (strcmp(argv[1], "support") == 0) {
		char *enabled_features;
        size_t bufsize = getpagesize() * 2;
        enabled_features = (char *)malloc(bufsize);
        if (!enabled_features) {
            perror("malloc");
            return -ENOMEM;
        }
        prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_ENABLED_FEATURES, enabled_features, bufsize, &error);
        printf("%s\n", error || !strlen(enabled_features) ? "Unsupported" : "Supported");
        free(enabled_features);
    } else if (argc == 3 && !strcmp(argv[1], "sus_su")) {
		int last_working_mode = 0;
		int target_working_mode;
		char* endptr;

		prctl(KERNEL_SU_OPTION, CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE, &last_working_mode, NULL, &error);
		if (error)
			return error;
		if (!strcmp(argv[2], "mode")) {
			printf("%d\n", last_working_mode);
			return 0;
		}
		target_working_mode = strtol(argv[2], &endptr, 10);
		if (*endptr != '\0') {
			return 1;
		}
		if (target_working_mode == SUS_SU_WITH_HOOKS) {
			bool is_sus_su_ready;
			prctl(KERNEL_SU_OPTION, CMD_SUSFS_IS_SUS_SU_READY, &is_sus_su_ready, NULL, &error);
			if (error)
				return error;
			if (!is_sus_su_ready) {
                printf("[-] sus_su mode %d has to be run during or after service stage\n", SUS_SU_WITH_HOOKS);
				return 1;
			}
			if (last_working_mode == SUS_SU_DISABLED) {
				error = enable_sus_su(last_working_mode, SUS_SU_WITH_HOOKS);
			} else if (last_working_mode == SUS_SU_WITH_HOOKS) {
                printf("[-] sus_su is already in mode %d\n", last_working_mode);
				return 1;
			} else {
				error = enable_sus_su(last_working_mode, SUS_SU_DISABLED);
				if (!error)
					error = enable_sus_su(last_working_mode, SUS_SU_WITH_HOOKS);
			}
		} else if (target_working_mode == SUS_SU_DISABLED) {
			if (last_working_mode == SUS_SU_DISABLED) {
                printf("[-] sus_su is already in mode %d\n", last_working_mode);
				return 1;
			}
			error = enable_sus_su(last_working_mode, SUS_SU_DISABLED);
		}
    } else {
        fprintf(stderr, "Invalid argument: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
