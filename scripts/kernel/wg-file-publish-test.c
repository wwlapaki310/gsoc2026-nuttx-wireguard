#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define FAR
#define WG_LINE_MAX 160

static int fault;
static int renames;
static int ready_fd;
static int resume_fd;

static int injected_rename(const char *src, const char *dst)
{
  renames++;
  if (fault == 10)
    {
      char token;
      assert(write(ready_fd, "x", 1) == 1);
      assert(read(resume_fd, &token, 1) == 1);
    }

  if (fault == 2 || fault == 3)
    {
      if (fault == 2)
        {
          /* NuttX mountptrename can remove dst before the FS fails. */
          assert(unlink(dst) == 0);
        }

      errno = EIO;
      return -1;
    }

  return rename(src, dst);
}

static FILE *injected_fopen(const char *path, const char *mode)
{
  if (fault == 4 && strcmp(path, "config") == 0)
    {
      errno = EACCES;
      return NULL;
    }

  return fopen(path, mode);
}

static size_t injected_fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
  if (fault == 5)
    {
      errno = ENOSPC;
      return 0;
    }

  return fwrite(buf, size, n, f);
}

static int injected_fclose(FILE *f)
{
  int ret = fclose(f);
  if (fault == 6)
    {
      errno = EIO;
      return EOF;
    }

  return ret;
}

static size_t injected_fread(void *buf, size_t size, size_t n, FILE *f)
{
  if (fault == 9)
    {
      errno = EIO;
      return 0;
    }

  return fread(buf, size, n, f);
}

static int injected_ferror(FILE *f)
{
  return fault == 9 ? 1 : ferror(f);
}

#define rename injected_rename
#define fopen injected_fopen
#define fwrite injected_fwrite
#define fclose injected_fclose
#define fread injected_fread
#define ferror injected_ferror
/* PUBLISHER_UNDER_TEST */
#undef rename
#undef fopen
#undef fwrite
#undef fclose
#undef fread
#undef ferror

static void put(const char *path, const char *contents)
{
  FILE *f = fopen(path, "w");
  assert(f != NULL);
  assert(fputs(contents, f) >= 0);
  assert(fclose(f) == 0);
}

static void matches(const char *path, const char *expected)
{
  char buf[80] = {0};
  FILE *f = fopen(path, "r");
  assert(f != NULL);
  assert(fread(buf, 1, sizeof(buf) - 1, f) == strlen(expected));
  assert(strcmp(buf, expected) == 0);
  assert(fclose(f) == 0);
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  char first[128];
  char second[128];
  char tiny[3];
  struct stat st;
  FILE *a = wg_open_temp(first, sizeof(first), "config");
  FILE *b = wg_open_temp(second, sizeof(second), "config");
  assert(a != NULL && b != NULL && strcmp(first, second) != 0);
  assert(stat(first, &st) == 0 && (st.st_mode & 0777) == 0600);
  assert(wg_open_temp(tiny, sizeof(tiny), "config") == NULL);
  assert(fclose(a) == 0 && fclose(b) == 0);
  assert(unlink(first) == 0 && unlink(second) == 0);
  put("record", "[Interface]\nPrivateKey = old\n[Peer]\nPublicKey = peer\n");
  assert(wg_record_private_key("record", "new") == 0);
  assert(wg_record_private_key("record", "new") == 0);
  matches("record", "[Interface]\nPrivateKey = new\n[Peer]\nPublicKey = peer\n");
  renames = 0;
  fault = atoi(argv[1]);
  if (fault != 0)
    {
      put("config", "old key and peers\n");
    }

  put("staged", "new key and peers\n");
  if (fault == 10)
    {
      int ready[2];
      int resume[2];
      int status;
      char token;
      assert(pipe(ready) == 0 && pipe(resume) == 0);
      alarm(10);
      pid_t child = fork();
      assert(child >= 0);
      if (child == 0)
        {
          ready_fd = ready[1];
          resume_fd = resume[0];
          _exit(wg_replace_file("staged", "config") != 0);
        }

      assert(read(ready[0], &token, 1) == 1);
      matches("config.bak", "old key and peers\n");
      put("second", "second writer\n");
      assert(wg_replace_file("second", "config") == -1);
      matches("config.bak", "old key and peers\n");
      matches("config", "old key and peers\n");
      assert(write(resume[1], "x", 1) == 1);
      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
      matches("config", "new key and peers\n");
      assert(access("config.bak", F_OK) != 0);
      puts("PASS: overlapping publishers, reservation held across rename");
      return 0;
    }

  if (fault == 7)
    {
      put("config.bak", "another writer or recovery\n");
    }

  if (fault == 8)
    {
      assert(unlink("staged") == 0);
    }

  int ret = wg_replace_file("staged", "config");
  if (fault <= 1)
    {
      assert(ret == 0);
      matches("config", "new key and peers\n");
      assert(access("config.bak", F_OK) != 0);
    }
  else
    {
      assert(ret == -1);
      if (fault == 2 || fault == 3 || fault == 8)
        {
          matches("config.bak", "old key and peers\n");
          if (fault != 8)
            {
              matches("staged", "new key and peers\n");
            }

          /* A retry must not overwrite the only recovery copy. */
          put("retry", "later key\n");
          int before = renames;
          assert(wg_replace_file("retry", "config") == -1);
          assert(renames == before);
          matches("config.bak", "old key and peers\n");
        }
      else
        {
          assert(renames == 0);
          matches("config", "old key and peers\n");
          if (fault == 7)
            {
              matches("config.bak", "another writer or recovery\n");
            }
          else
            {
              assert(access("config.bak", F_OK) != 0);
            }
        }
    }

  printf("PASS: publisher fault case %d\n", fault);
  return 0;
}
