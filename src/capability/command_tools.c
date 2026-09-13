/*
 * 白名单命令工具：只 exec 已声明 argv（相对 root 或绝对路径），支持 stdin_json / env，不拼 shell。
 */
#include "command_tools.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__APPLE__) && !defined(__linux__)
int command_tool_find(const agent_config_t *conf, const char *name) {
  (void)conf;
  (void)name;
  return -1;
}
int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd,
                     const char *args_json, char **out_text, size_t *out_len,
                     int timeout_override_sec) {
  (void)conf;
  (void)root_real;
  (void)cmd;
  (void)args_json;
  (void)timeout_override_sec;
  if (out_text) *out_text = NULL;
  if (out_len) *out_len = 0;
  return -1;
}
#else

int command_tool_find(const agent_config_t *conf, const char *name) {
  int i;
  if (!conf || !name) return -1;
  for (i = 0; i < conf->tools.command_count; i++) {
    if (conf->tools.commands[i].name && strcmp(conf->tools.commands[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int path_has_dotdot(const char *p) {
  const char *s = p;
  if (!s) return 1;
  while (*s) {
    if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == '\0')) return 1;
    while (*s && *s != '/') s++;
    if (*s == '/') s++;
  }
  return 0;
}

/* 解析 argv[0]：
 * - 相对路径：拼到 root 下并 realpath，须仍落在 root 内（与读写沙箱一致）。
 * - 绝对路径：允许（能力文件即白名单）；realpath 后须为普通文件且可执行；禁止含 .. 段。
 * 不拼 shell、不查 PATH 裸名。 */
static int resolve_argv0(const char *root_real, const char *argv0, char *out, size_t out_sz) {
  char joined[PATH_MAX];
  char resolved[PATH_MAX];
  size_t root_len;
  struct stat st;
  if (!root_real || !argv0 || !argv0[0] || !out) return -1;
  if (path_has_dotdot(argv0)) return -1;

  if (argv0[0] == '/') {
    if (!realpath(argv0, resolved)) return -1;
    if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
    if (access(resolved, X_OK) != 0) return -1;
    if (strlen(resolved) + 1 > out_sz) return -1;
    memcpy(out, resolved, strlen(resolved) + 1);
    return 0;
  }

  if (snprintf(joined, sizeof(joined), "%s/%s", root_real, argv0) >= (int)sizeof(joined))
    return -1;
  if (!realpath(joined, resolved)) return -1;
  root_len = strlen(root_real);
  if (strncmp(resolved, root_real, root_len) != 0) return -1;
  if (resolved[root_len] != '\0' && resolved[root_len] != '/') return -1;
  if (strlen(resolved) + 1 > out_sz) return -1;
  memcpy(out, resolved, strlen(resolved) + 1);
  return 0;
}

static char *dup_err(const char *msg) {
  size_t n = strlen(msg) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, msg, n);
  return p;
}

int command_tool_run(const agent_config_t *conf, const char *root_real, const tool_command_t *cmd,
                     const char *args_json, char **out_text, size_t *out_len,
                     int timeout_override_sec) {
  char exe[PATH_MAX];
  char **child_argv = NULL;
  int pipe_out[2] = {-1, -1};
  int pipe_in[2] = {-1, -1};
  pid_t pid;
  int status = 0;
  int timed_out = 0;
  int max_out;
  int timeout_sec;
  char *buf = NULL;
  size_t cap = 0, len = 0;
  const char *payload;
  ssize_t nread;
  int i;

  if (out_text) *out_text = NULL;
  if (out_len) *out_len = 0;
  if (!conf || !root_real || !cmd || !out_text) return -1;
  if (cmd->argv_count < 1 || !cmd->argv || !cmd->argv[0]) {
    *out_text = dup_err("ERROR: empty argv");
    return -1;
  }
  if (resolve_argv0(root_real, cmd->argv[0], exe, sizeof(exe)) != 0) {
    *out_text = dup_err("ERROR: path not allowed");
    return -1;
  }

  payload = (args_json && args_json[0]) ? args_json : "{}";
  max_out = cmd->max_output_bytes > 0 ? cmd->max_output_bytes : 65536;
  /* override 仅本趟有效，不改 cmd 结构体 */
  timeout_sec = cmd->timeout_sec > 0 ? cmd->timeout_sec : 30;
  if (timeout_override_sec > 0) timeout_sec = timeout_override_sec;
  if (timeout_sec > 600) timeout_sec = 600;

  child_argv = calloc((size_t)cmd->argv_count + 1, sizeof(char *));
  if (!child_argv) {
    *out_text = dup_err("ERROR: oom");
    return -1;
  }
  child_argv[0] = exe;
  for (i = 1; i < cmd->argv_count; i++) child_argv[i] = cmd->argv[i];
  child_argv[cmd->argv_count] = NULL;

  if (pipe(pipe_out) != 0) {
    free(child_argv);
    *out_text = dup_err("ERROR: pipe failed");
    return -1;
  }
  if (cmd->pass_args == 0) {
    if (pipe(pipe_in) != 0) {
      close(pipe_out[0]);
      close(pipe_out[1]);
      free(child_argv);
      *out_text = dup_err("ERROR: pipe failed");
      return -1;
    }
  }

  pid = fork();
  if (pid < 0) {
    close(pipe_out[0]);
    close(pipe_out[1]);
    if (pipe_in[0] >= 0) {
      close(pipe_in[0]);
      close(pipe_in[1]);
    }
    free(child_argv);
    *out_text = dup_err("ERROR: fork failed");
    return -1;
  }

  if (pid == 0) {
    /* child */
    if (chdir(root_real) != 0) _exit(127);
    close(pipe_out[0]);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_out[1], STDERR_FILENO);
    close(pipe_out[1]);
    if (cmd->pass_args == 0) {
      close(pipe_in[1]);
      dup2(pipe_in[0], STDIN_FILENO);
      close(pipe_in[0]);
    } else {
      int devnull = open("/dev/null", O_RDONLY);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        close(devnull);
      }
      setenv("NEO_TOOL_ARGS", payload, 1);
    }
    setenv("NEO_TOOL_NAME", cmd->name ? cmd->name : "", 1);
    execvp(child_argv[0], child_argv);
    _exit(127);
  }

  /* parent */
  free(child_argv);
  close(pipe_out[1]);
  if (cmd->pass_args == 0) {
    size_t plen = strlen(payload);
    size_t off = 0;
    close(pipe_in[0]);
    while (off < plen) {
      ssize_t w = write(pipe_in[1], payload + off, plen - off);
      if (w < 0) {
        if (errno == EINTR) continue;
        break;
      }
      off += (size_t)w;
    }
    close(pipe_in[1]);
  }

  {
    int elapsed = 0;
    int done = 0;
    cap = 4096;
    buf = malloc(cap);
    if (!buf) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      close(pipe_out[0]);
      *out_text = dup_err("ERROR: oom");
      return -1;
    }
    buf[0] = '\0';
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
    while (!done) {
      fd_set rfds;
      struct timeval tv;
      int sel;
      FD_ZERO(&rfds);
      FD_SET(pipe_out[0], &rfds);
      tv.tv_sec = 0;
      tv.tv_usec = 200000; /* 200ms */
      sel = select(pipe_out[0] + 1, &rfds, NULL, NULL, &tv);
      if (sel > 0 && FD_ISSET(pipe_out[0], &rfds)) {
        char tmp[4096];
        nread = read(pipe_out[0], tmp, sizeof(tmp));
        if (nread > 0) {
          size_t room = (size_t)max_out > len ? (size_t)max_out - len : 0;
          size_t take = (size_t)nread < room ? (size_t)nread : room;
          if (take > 0) {
            if (len + take + 1 > cap) {
              size_t ncap = cap * 2;
              char *nb;
              while (ncap < len + take + 1) ncap *= 2;
              nb = realloc(buf, ncap);
              if (!nb) break;
              buf = nb;
              cap = ncap;
            }
            memcpy(buf + len, tmp, take);
            len += take;
            buf[len] = '\0';
          }
        } else if (nread == 0) {
          done = 1;
        }
      }
      {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
          /* drain remaining */
          for (;;) {
            char tmp[4096];
            nread = read(pipe_out[0], tmp, sizeof(tmp));
            if (nread <= 0) break;
            {
              size_t room = (size_t)max_out > len ? (size_t)max_out - len : 0;
              size_t take = (size_t)nread < room ? (size_t)nread : room;
              if (take == 0) break;
              if (len + take + 1 > cap) {
                size_t ncap = cap * 2;
                char *nb;
                while (ncap < len + take + 1) ncap *= 2;
                nb = realloc(buf, ncap);
                if (!nb) break;
                buf = nb;
                cap = ncap;
              }
              memcpy(buf + len, tmp, take);
              len += take;
              buf[len] = '\0';
            }
          }
          done = 1;
        } else {
          elapsed += 200;
          if (elapsed >= timeout_sec * 1000) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = 1;
            done = 1;
          }
        }
      }
    }
    close(pipe_out[0]);
  }

  if (timed_out) {
    free(buf);
    *out_text = dup_err("ERROR: timeout");
    if (out_len) *out_len = strlen(*out_text);
    return -1;
  }

  {
    int code = 0;
    char prefix[64];
    size_t prelen;
    char *final;
    if (WIFEXITED(status))
      code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
      code = 128 + WTERMSIG(status);
    if (code != 0) {
      snprintf(prefix, sizeof(prefix), "EXIT:%d\n", code);
      prelen = strlen(prefix);
      final = malloc(prelen + len + 1);
      if (!final) {
        free(buf);
        *out_text = dup_err("ERROR: oom");
        return -1;
      }
      memcpy(final, prefix, prelen);
      if (len) memcpy(final + prelen, buf, len);
      final[prelen + len] = '\0';
      free(buf);
      *out_text = final;
      if (out_len) *out_len = prelen + len;
    } else {
      *out_text = buf ? buf : dup_err("");
      if (out_len) *out_len = len;
    }
  }
  return 0;
}

#endif
