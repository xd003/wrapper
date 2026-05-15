#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cmdline.h"

#define CAP_SYS_ADMIN_IDX 21
#define CAP_SYS_ADMIN_BIT (1ULL << CAP_SYS_ADMIN_IDX)
#define MAX_ACCOUNTS 32

static pid_t child_pids[MAX_ACCOUNTS];
static int num_children = 0;
static struct gengetopt_args_info args_info;

static void term_children(int signum) {
    (void)signum;
    for (int i = 0; i < num_children; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }
}

static int has_cap_sys_admin(void) {
    FILE *fp = fopen("/proc/self/status", "r");
    if (!fp) return 0;

    char line[256];
    unsigned long long cap_eff = 0;
    int found = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "CapEff:", 7) == 0) {
            char *p = line + 7;
            while (*p == '\t' || *p == ' ') p++;
            cap_eff = strtoull(p, NULL, 16);
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found && (cap_eff & CAP_SYS_ADMIN_BIT);
}

static int parse_accounts(const char *login_arg, char ***out) {
    if (!login_arg || !*login_arg) {
        *out = NULL;
        return 1;
    }

    int count = 1;
    for (const char *p = login_arg; *p; p++) {
        if (*p == ',') count++;
    }
    if (count > MAX_ACCOUNTS) {
        fprintf(stderr, "[supervisor] too many accounts (max %d)\n", MAX_ACCOUNTS);
        exit(1);
    }

    char **arr = calloc(count, sizeof(char *));
    if (!arr) {
        perror("calloc");
        exit(1);
    }

    char *copy = strdup(login_arg);
    if (!copy) {
        perror("strdup");
        exit(1);
    }

    int i = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(copy, ",", &saveptr); tok != NULL;
         tok = strtok_r(NULL, ",", &saveptr)) {
        while (*tok == ' ' || *tok == '\t') tok++;
        arr[i++] = strdup(tok);
    }
    free(copy);

    *out = arr;
    return i;
}

int main(int argc, char *argv[], char *envp[]) {
    cmdline_parser(argc, argv, &args_info);

    if (signal(SIGINT, term_children) == SIG_ERR ||
        signal(SIGTERM, term_children) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    if (mkdir("./rootfs/dev", 0755) != 0 && errno != EEXIST) {
        perror("mkdir ./rootfs/dev failed");
        return 1;
    }

    int fd = open("./rootfs/dev/urandom", O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("open ./rootfs/dev/urandom failed");
        return 1;
    }
    close(fd);

    if (mount("/dev/urandom", "./rootfs/dev/urandom", NULL, MS_BIND, NULL) != 0) {
        perror("mount /dev/urandom failed");
        return 1;
    }

    if (chdir("./rootfs") != 0) {
        perror("chdir");
        return 1;
    }
    if (chroot("./") != 0) {
        perror("chroot");
        return 1;
    }

    if (mkdir("/proc", 0755) != 0 && errno != EEXIST) {
        perror("mkdir /proc failed");
        return 1;
    }

    chmod("/system/bin/linker64", 0755);
    chmod("/system/bin/main", 0755);

    if (has_cap_sys_admin()) {
        if (unshare(CLONE_NEWPID)) {
            perror("unshare");
            return 1;
        }
    }

    char **accounts = NULL;
    int n = parse_accounts(args_info.login_arg, &accounts);
    num_children = n;
    for (int i = 0; i < MAX_ACCOUNTS; i++) child_pids[i] = -1;

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            term_children(0);
            return 1;
        }

        if (pid == 0) {
            if (mount("proc", "/proc", "proc", 0, NULL) != 0 && errno != EBUSY) {
                perror("mount proc failed");
                return 1;
            }

            int decrypt_port = args_info.decrypt_port_arg + i;
            int m3u8_port = args_info.m3u8_port_arg + i;
            int account_port = args_info.account_port_arg + i;

            char base_dir[1024];
            if (n > 1) {
                snprintf(base_dir, sizeof(base_dir), "%s/account_%d", args_info.base_dir_arg, i);
            } else {
                snprintf(base_dir, sizeof(base_dir), "%s", args_info.base_dir_arg);
            }
            if (mkdir(base_dir, 0777) != 0 && errno != EEXIST) {
                perror("mkdir base_dir failed");
            }

            char db_dir[1100];
            snprintf(db_dir, sizeof(db_dir), "%s/mpl_db", base_dir);
            if (mkdir(db_dir, 0777) != 0 && errno != EEXIST) {
                perror("mkdir mpl_db failed");
            }

            char decrypt_str[16], m3u8_str[16], account_str[16];
            snprintf(decrypt_str, sizeof(decrypt_str), "%d", decrypt_port);
            snprintf(m3u8_str, sizeof(m3u8_str), "%d", m3u8_port);
            snprintf(account_str, sizeof(account_str), "%d", account_port);

            char *new_argv[32];
            int idx = 0;
            new_argv[idx++] = "/system/bin/main";
            if (accounts && accounts[i]) {
                new_argv[idx++] = "-L";
                new_argv[idx++] = accounts[i];
            }
            new_argv[idx++] = "-D";
            new_argv[idx++] = decrypt_str;
            new_argv[idx++] = "-M";
            new_argv[idx++] = m3u8_str;
            new_argv[idx++] = "-A";
            new_argv[idx++] = account_str;
            new_argv[idx++] = "-B";
            new_argv[idx++] = base_dir;
            new_argv[idx++] = "-H";
            new_argv[idx++] = args_info.host_arg;
            if (args_info.proxy_arg && *args_info.proxy_arg) {
                new_argv[idx++] = "-P";
                new_argv[idx++] = args_info.proxy_arg;
            }
            if (args_info.code_from_file_flag) {
                new_argv[idx++] = "-F";
            }
            if (args_info.device_info_arg) {
                new_argv[idx++] = "-I";
                new_argv[idx++] = args_info.device_info_arg;
            }
            new_argv[idx] = NULL;

            execve("/system/bin/main", new_argv, envp);
            perror("execve");
            return 1;
        }

        child_pids[i] = pid;
        fprintf(stderr,
                "[supervisor] account %d pid=%d ports decrypt=%d m3u8=%d account=%d\n",
                i, pid,
                args_info.decrypt_port_arg + i,
                args_info.m3u8_port_arg + i,
                args_info.account_port_arg + i);
    }

    int status = 0;
    pid_t dead = wait(&status);
    fprintf(stderr, "[supervisor] child pid=%d exited (status=0x%x); terminating siblings\n",
            dead, status);

    for (int i = 0; i < num_children; i++) {
        if (child_pids[i] > 0 && child_pids[i] != dead) {
            kill(child_pids[i], SIGTERM);
        }
    }
    while (wait(NULL) > 0) { }

    if (accounts) {
        for (int i = 0; i < n; i++) free(accounts[i]);
        free(accounts);
    }

    return 0;
}
