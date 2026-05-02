#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

const char* get_best_temp_dir() {
    char *env_tmp = getenv("TMPDIR");
    if (env_tmp && access(env_tmp, W_OK) == 0) {
        return env_tmp;
    }

    const char *termux_tmp = "/data/data/com.termux/files/usr/tmp";
    if (access(termux_tmp, W_OK) == 0) {
        return termux_tmp;
    }

    const char *android_tmp = "/data/local/tmp";
    if (access(android_tmp, W_OK) == 0) {
        return android_tmp;
    }

    return "/tmp";
}

// 辅助函数：安全创建目录并检查错误
static int safe_mkdir(const char *path, mode_t mode) {
    if (mkdir(path, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "[-] mkdir %s failed: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

void run_proot_encapsulated(char *target_binary, char **extra_args) {
    char *proot_path = "./android/proot";

    // 1. 检查 proot 引擎本身是否存在且拥有执行权限
    if (access(proot_path, X_OK) != 0) {
        fprintf(stderr, "[-] PRoot binary not found or not executable at %s\n", proot_path);
        exit(EXIT_FAILURE);
    }

    // 2. 检查宿主机的源映射文件是否存在，避免 proot 内部静默失败
    if (access("android/libnetd_client.so", R_OK) != 0) {
        fprintf(stderr, "[-] Warning: Source file android/libnetd_client.so not found or not readable!\n");
    }

    // 3. 严格创建 PRoot 挂载所需的基础目录树
    // 注意：如果当前工作目录是只读的，这里的 safe_mkdir 会报错并中断程序，避免后续的无头苍蝇式报错
    if (safe_mkdir("rootfs", 0755) != 0) exit(EXIT_FAILURE);
    if (safe_mkdir("rootfs/dev", 0755) != 0) exit(EXIT_FAILURE);
    if (safe_mkdir("rootfs/proc", 0755) != 0) exit(EXIT_FAILURE);
    if (safe_mkdir("rootfs/sys", 0755) != 0) exit(EXIT_FAILURE);
    
    // 为 -b android/libnetd_client.so:/system/lib64/libnetd_client.so 创建父级目录结构
    if (safe_mkdir("rootfs/system", 0755) != 0) exit(EXIT_FAILURE);
    if (safe_mkdir("rootfs/system/lib64", 0755) != 0) exit(EXIT_FAILURE);

    char proot_tmp_env[512];
    const char *tmp_dir = get_best_temp_dir();
    snprintf(proot_tmp_env, sizeof(proot_tmp_env), "PROOT_TMP_DIR=%s", tmp_dir);
    printf("[*] Auto-setting %s\n", proot_tmp_env);

    char *argv[] = {
        "proot",
        "-r", "rootfs/",
        "-b", "/dev:/dev",
        "-b", "/proc:/proc",
        "-b", "/sys:/sys",
        "-b", "android/libnetd_client.so:/system/lib64/libnetd_client.so", 
        "-w", "/",
        target_binary,
        NULL
    };

    char *envp[] = {
        proot_tmp_env,
        NULL
    };

    pid_t pid = fork();

    if (pid == 0) {
        if (execve(proot_path, argv, envp) == -1) {
            perror("execve proot failed");
            exit(EXIT_FAILURE);
        }
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("[+] PRoot exited with status %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[-] PRoot killed by signal %d\n", WTERMSIG(status));
        }
    } else {
        perror("fork failed");
    }
}

int main() {
    run_proot_encapsulated("/system/bin/main", NULL);
    return 0;
}